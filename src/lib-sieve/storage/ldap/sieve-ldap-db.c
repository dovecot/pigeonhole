/* Copyright (c) 2002-2018 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"

#include "sieve-common.h"

#include "sieve-ldap-storage.h"

/* FIXME: Imported this from Dovecot auth for now. We're working on a proper
   lib-ldap, but, until then, some code is duplicated here. */

#if defined(SIEVE_BUILTIN_LDAP) || defined(PLUGIN_BUILD)

#include "net.h"
#include "ioloop.h"
#include "array.h"
#include "hash.h"
#include "aqueue.h"
#include "str.h"
#include "time-util.h"
#include "env-util.h"
#include "var-expand.h"
#include "istream.h"

#include <stddef.h>
#include <unistd.h>

struct db_ldap_result {
	int refcount;
	LDAPMessage *msg;
};

struct db_ldap_result_iterate_context {
	pool_t pool;

	struct auth_request *auth_request;
	const ARRAY_TYPE(ldap_field) *attr_map;
	unsigned int attr_idx;

	/* attribute name => value */
	HASH_TABLE(char *, struct db_ldap_value *) ldap_attrs;

	const char *val_1_arr[2];
	string_t *var, *debug;

	bool skip_null_values;
	bool iter_dn_values;
};

struct db_ldap_sasl_bind_context {
	const char *authcid;
	const char *passwd;
	const char *realm;
	const char *authzid;
};

static struct ldap_connection *ldap_connections = NULL;

static int db_ldap_bind(struct ldap_connection *conn);
static void db_ldap_conn_close(struct ldap_connection *conn);

static int ldap_get_errno(struct ldap_connection *conn)
{
	struct sieve_storage *storage = &conn->lstorage->storage;
	int ret, err;

	ret = ldap_get_option(conn->ld, LDAP_OPT_ERROR_NUMBER, &err);
	if (ret != LDAP_SUCCESS) {
		e_error(storage->event, "db: "
			"Can't get error number: %s",
			ldap_err2string(ret));
		return LDAP_UNAVAILABLE;
	}

	return err;
}

const char *ldap_get_error(struct ldap_connection *conn)
{
	const char *ret;
	char *str = NULL;

	ret = ldap_err2string(ldap_get_errno(conn));

	ldap_get_option(conn->ld, LDAP_OPT_ERROR_STRING, &str);
	if (str != NULL) {
		ret = t_strconcat(ret, ", ", str, NULL);
		ldap_memfree(str);
	}
	ldap_set_option(conn->ld, LDAP_OPT_ERROR_STRING, NULL);
	return ret;
}

static void ldap_conn_reconnect(struct ldap_connection *conn)
{
	db_ldap_conn_close(conn);
	if (sieve_ldap_db_connect(conn) < 0)
		db_ldap_conn_close(conn);
}

static int ldap_handle_error(struct ldap_connection *conn)
{
	int err = ldap_get_errno(conn);

	switch (err) {
	case LDAP_SUCCESS:
		i_unreached();
	case LDAP_SIZELIMIT_EXCEEDED:
	case LDAP_TIMELIMIT_EXCEEDED:
	case LDAP_NO_SUCH_ATTRIBUTE:
	case LDAP_UNDEFINED_TYPE:
	case LDAP_INAPPROPRIATE_MATCHING:
	case LDAP_CONSTRAINT_VIOLATION:
	case LDAP_TYPE_OR_VALUE_EXISTS:
	case LDAP_INVALID_SYNTAX:
	case LDAP_NO_SUCH_OBJECT:
	case LDAP_ALIAS_PROBLEM:
	case LDAP_INVALID_DN_SYNTAX:
	case LDAP_IS_LEAF:
	case LDAP_ALIAS_DEREF_PROBLEM:
	case LDAP_FILTER_ERROR:
		/* Invalid input */
		return -1;
	case LDAP_SERVER_DOWN:
	case LDAP_TIMEOUT:
	case LDAP_UNAVAILABLE:
	case LDAP_BUSY:
#ifdef LDAP_CONNECT_ERROR
	case LDAP_CONNECT_ERROR:
#endif
	case LDAP_LOCAL_ERROR:
	case LDAP_INVALID_CREDENTIALS:
	case LDAP_OPERATIONS_ERROR:
	default:
		/* Connection problems */
		ldap_conn_reconnect(conn);
		return 0;
	}
}

static int db_ldap_request_search(struct ldap_connection *conn,
				  struct ldap_request *request)
{
	struct sieve_storage *storage = &conn->lstorage->storage;

	i_assert(conn->conn_state == LDAP_CONN_STATE_BOUND);
	i_assert(request->msgid == -1);

	request->msgid =
		ldap_search(conn->ld, *request->base == '\0' ? NULL :
			    request->base, request->scope,
			    request->filter, request->attributes, 0);
	if (request->msgid == -1) {
		e_error(storage->event, "db: "
			"ldap_search(%s) parsing failed: %s",
			request->filter, ldap_get_error(conn));
		if (ldap_handle_error(conn) < 0) {
			/* Broken request, remove it */
			return 0;
		}
		return -1;
	}
	return 1;
}

static bool db_ldap_request_queue_next(struct ldap_connection *conn)
{
	struct ldap_request *const *requestp, *request;
	int ret = -1;

	/* Connecting may call db_ldap_connect_finish(), which gets us back
	   here. so do the connection before checking the request queue. */
	if (sieve_ldap_db_connect(conn) < 0)
		return FALSE;

	if (conn->pending_count == aqueue_count(conn->request_queue)) {
		/* No non-pending requests */
		return FALSE;
	}
	if (conn->pending_count > DB_LDAP_MAX_PENDING_REQUESTS) {
		/* Wait until server has replied to some requests */
		return FALSE;
	}

	requestp = array_idx(&conn->request_array,
			     aqueue_idx(conn->request_queue,
					conn->pending_count));
	request = *requestp;

	switch (conn->conn_state) {
	case LDAP_CONN_STATE_DISCONNECTED:
	case LDAP_CONN_STATE_BINDING:
		/* Wait until we're in bound state */
		return FALSE;
	case LDAP_CONN_STATE_BOUND:
		/* We can do anything in this state */
		break;
	}

	ret = db_ldap_request_search(conn, request);
	if (ret > 0) {
		/* Success */
		i_assert(request->msgid != -1);
		conn->pending_count++;
		return TRUE;
	} else if (ret < 0) {
		/* Disconnected */
		return FALSE;
	} else {
		/* Broken request, remove from queue */
		aqueue_delete_tail(conn->request_queue);
		request->callback(conn, request, NULL);
		return TRUE;
	}
}

static bool db_ldap_check_limits(struct ldap_connection *conn)
{
	struct sieve_storage *storage = &conn->lstorage->storage;
	struct ldap_request *const *first_requestp;
	unsigned int count;
	time_t secs_diff;

	count = aqueue_count(conn->request_queue);
	if (count == 0)
		return TRUE;

	first_requestp = array_idx(&conn->request_array,
				   aqueue_idx(conn->request_queue, 0));
	secs_diff = ioloop_time - (*first_requestp)->create_time;
	if (secs_diff > DB_LDAP_REQUEST_LOST_TIMEOUT_SECS) {
		e_error(storage->event, "db: "
			"Connection appears to be hanging, reconnecting");
		ldap_conn_reconnect(conn);
		return TRUE;
	}
	return TRUE;
}

void db_ldap_request(struct ldap_connection *conn,
		     struct ldap_request *request)
{
	request->msgid = -1;
	request->create_time = ioloop_time;

	if (!db_ldap_check_limits(conn)) {
		request->callback(conn, request, NULL);
		return;
	}

	aqueue_append(conn->request_queue, &request);
	(void)db_ldap_request_queue_next(conn);
}

static int db_ldap_connect_finish(struct ldap_connection *conn, int ret)
{
	struct sieve_storage *storage = &conn->lstorage->storage;
	const struct sieve_ldap_settings *set = conn->lstorage->ldap_set;

	if (ret == LDAP_SERVER_DOWN) {
		e_error(storage->event, "db: "
			"Can't connect to server: %s", set->uris);
		return -1;
	}
	if (ret != LDAP_SUCCESS) {
		e_error(storage->event, "db: "
			"binding failed (dn %s): %s",
			*set->auth_dn == '\0' ? "(none)" : set->auth_dn,
			ldap_get_error(conn));
		return -1;
	}

	timeout_remove(&conn->to);
	conn->conn_state = LDAP_CONN_STATE_BOUND;
	e_debug(storage->event, "db: "
		"Successfully bound (dn %s)",
		*set->auth_dn == '\0' ? "(none)" : set->auth_dn);
	while (db_ldap_request_queue_next(conn))
		;
	return 0;
}

static void
db_ldap_default_bind_finished(struct ldap_connection *conn,
			      struct db_ldap_result *res)
{
	int ret;

	i_assert(conn->pending_count == 0);
	conn->default_bind_msgid = -1;

	ret = ldap_result2error(conn->ld, res->msg, FALSE);
	if (db_ldap_connect_finish(conn, ret) < 0) {
		/* Lost connection, close it */
		db_ldap_conn_close(conn);
	}
}

static void
db_ldap_abort_requests(struct ldap_connection *conn, unsigned int max_count,
		       unsigned int timeout_secs, bool error, const char *reason)
{
	struct sieve_storage *storage = &conn->lstorage->storage;
	struct ldap_request *const *requestp, *request;
	time_t diff;

	while (aqueue_count(conn->request_queue) > 0 && max_count > 0) {
		requestp = array_idx(&conn->request_array,
				     aqueue_idx(conn->request_queue, 0));
		request = *requestp;

		diff = ioloop_time - request->create_time;
		if (diff < (time_t)timeout_secs)
			break;

		/* timed out, abort */
		aqueue_delete_tail(conn->request_queue);

		if (request->msgid != -1) {
			i_assert(conn->pending_count > 0);
			conn->pending_count--;
		}
		if (error)
			e_error(storage->event, "db: %s", reason);
		else
			e_debug(storage->event, "db: %s", reason);
		request->callback(conn, request, NULL);
		max_count--;
	}
}

static struct ldap_request *
db_ldap_find_request(struct ldap_connection *conn, int msgid,
		     unsigned int *idx_r)
{
	struct ldap_request *const *requests, *request = NULL;
	unsigned int i, count;

	count = aqueue_count(conn->request_queue);
	if (count == 0)
		return NULL;

	requests = array_idx(&conn->request_array, 0);
	for (i = 0; i < count; i++) {
		request = requests[aqueue_idx(conn->request_queue, i)];
		if (request->msgid == msgid) {
			*idx_r = i;
			return request;
		}
		if (request->msgid == -1)
			break;
	}
	return NULL;
}

static bool
db_ldap_handle_request_result(struct ldap_connection *conn,
			      struct ldap_request *request, unsigned int idx,
			      struct db_ldap_result *res)
{
	struct sieve_storage *storage = &conn->lstorage->storage;
	int ret;
	bool final_result;

	i_assert(conn->pending_count > 0);

	switch (ldap_msgtype(res->msg)) {
	case LDAP_RES_SEARCH_ENTRY:
	case LDAP_RES_SEARCH_RESULT:
		break;
	case LDAP_RES_SEARCH_REFERENCE:
		/* We're going to ignore this */
		return FALSE;
	default:
		e_error(storage->event, "db: Reply with unexpected type %d",
			ldap_msgtype(res->msg));
		return TRUE;
	}

	if (ldap_msgtype(res->msg) == LDAP_RES_SEARCH_ENTRY) {
		ret = LDAP_SUCCESS;
		final_result = FALSE;
	} else {
		final_result = TRUE;
		ret = ldap_result2error(conn->ld, res->msg, 0);
	}
	if (ret != LDAP_SUCCESS) {
		/* Handle search failures here */
		e_error(storage->event, "db: "
			"ldap_search(base=%s filter=%s) failed: %s",
			request->base, request->filter,
			ldap_err2string(ret));
		res = NULL;
	} else {
		if (!final_result && storage->svinst->debug) {
			e_debug(storage->event,
				"db: ldap_search(base=%s filter=%s) returned entry: %s",
				request->base, request->filter,
				ldap_get_dn(conn->ld, res->msg));
		}
	}
	if (res == NULL && !final_result) {
		/* Wait for the final reply */
		request->failed = TRUE;
		return TRUE;
	}
	if (request->failed)
		res = NULL;
	if (final_result) {
		conn->pending_count--;
		aqueue_delete(conn->request_queue, idx);
	}

	T_BEGIN {
		request->callback(conn, request, res == NULL ? NULL : res->msg);
	} T_END;

	if (idx > 0) {
		/* See if there are timed out requests */
		db_ldap_abort_requests(conn, idx,
				       DB_LDAP_REQUEST_LOST_TIMEOUT_SECS,
				       TRUE, "Request lost");
	}
	return TRUE;
}

static void db_ldap_result_unref(struct db_ldap_result **_res)
{
	struct db_ldap_result *res = *_res;

	*_res = NULL;
	i_assert(res->refcount > 0);
	if (--res->refcount == 0) {
		ldap_msgfree(res->msg);
		i_free(res);
	}
}

static void db_ldap_request_free(struct ldap_request *request)
{
	if (request->result != NULL)
		db_ldap_result_unref(&request->result);
}

static void
db_ldap_handle_result(struct ldap_connection *conn, struct db_ldap_result *res)
{
	struct sieve_storage *storage = &conn->lstorage->storage;
	struct ldap_request *request;
	unsigned int idx;
	int msgid;

	msgid = ldap_msgid(res->msg);
	if (msgid == conn->default_bind_msgid) {
		db_ldap_default_bind_finished(conn, res);
		return;
	}

	request = db_ldap_find_request(conn, msgid, &idx);
	if (request == NULL) {
		e_error(storage->event,
			"db: Reply with unknown msgid %d", msgid);
		return;
	}

	if (db_ldap_handle_request_result(conn, request, idx, res))
		db_ldap_request_free(request);
}

static void ldap_input(struct ldap_connection *conn)
{
	struct sieve_storage *storage = &conn->lstorage->storage;
	struct timeval timeout;
	struct db_ldap_result *res;
	LDAPMessage *msg;
	time_t prev_reply_diff;
	int ret;

	do {
		if (conn->ld == NULL)
			return;

		i_zero(&timeout);
		ret = ldap_result(conn->ld, LDAP_RES_ANY, 0, &timeout, &msg);
#ifdef OPENLDAP_ASYNC_WORKAROUND
		if (ret == 0) {
			/* Try again, there may be another in buffer */
			ret = ldap_result(conn->ld, LDAP_RES_ANY, 0,
					  &timeout, &msg);
		}
#endif
		if (ret <= 0)
			break;

		res = i_new(struct db_ldap_result, 1);
		res->refcount = 1;
		res->msg = msg;
		db_ldap_handle_result(conn, res);
		db_ldap_result_unref(&res);
	} while (conn->io != NULL);

	prev_reply_diff = ioloop_time - conn->last_reply_stamp;
	conn->last_reply_stamp = ioloop_time;

	if (ret > 0) {
		/* Input disabled, continue once it's enabled */
		i_assert(conn->io == NULL);
	} else if (ret == 0) {
		/* Send more requests */
		while (db_ldap_request_queue_next(conn))
			;
	} else if (ldap_get_errno(conn) != LDAP_SERVER_DOWN) {
		e_error(storage->event, "db: ldap_result() failed: %s",
			ldap_get_error(conn));
		ldap_conn_reconnect(conn);
	} else if (aqueue_count(conn->request_queue) > 0 ||
		   prev_reply_diff < DB_LDAP_IDLE_RECONNECT_SECS) {
		e_error(storage->event,
			"db: Connection lost to LDAP server, reconnecting");
		ldap_conn_reconnect(conn);
	} else {
		/* Server probably disconnected an idle connection. don't
		   reconnect until the next request comes. */
		db_ldap_conn_close(conn);
	}
}

#ifdef HAVE_LDAP_SASL
static int
sasl_interact(LDAP *ld ATTR_UNUSED, unsigned flags ATTR_UNUSED,
	      void *defaults, void *interact)
{
	struct db_ldap_sasl_bind_context *context = defaults;
	sasl_interact_t *in;
	const char *str;

	for (in = interact; in->id != SASL_CB_LIST_END; in++) {
		switch (in->id) {
		case SASL_CB_GETREALM:
			str = context->realm;
			break;
		case SASL_CB_AUTHNAME:
			str = context->authcid;
			break;
		case SASL_CB_USER:
			str = context->authzid;
			break;
		case SASL_CB_PASS:
			str = context->passwd;
			break;
		default:
			str = NULL;
			break;
		}
		if (str != NULL) {
			in->len = strlen(str);
			in->result = str;
		}
	}
	return LDAP_SUCCESS;
}
#endif

static void ldap_connection_timeout(struct ldap_connection *conn)
{
	struct sieve_storage *storage = &conn->lstorage->storage;
	i_assert(conn->conn_state == LDAP_CONN_STATE_BINDING);

	e_error(storage->event, "db: Initial binding to LDAP server timed out");
	db_ldap_conn_close(conn);
}

static int db_ldap_bind(struct ldap_connection *conn)
{
	const struct sieve_ldap_settings *set = conn->lstorage->ldap_set;
	int msgid;

	i_assert(conn->conn_state != LDAP_CONN_STATE_BINDING);
	i_assert(conn->default_bind_msgid == -1);
	i_assert(conn->pending_count == 0);

	msgid = ldap_bind(conn->ld, set->auth_dn, set->auth_dn_password,
			  LDAP_AUTH_SIMPLE);
	if (msgid == -1) {
		i_assert(ldap_get_errno(conn) != LDAP_SUCCESS);
		if (db_ldap_connect_finish(conn, ldap_get_errno(conn)) < 0) {
			/* Lost connection, close it */
			db_ldap_conn_close(conn);
		}
		return -1;
	}

	conn->conn_state = LDAP_CONN_STATE_BINDING;
	conn->default_bind_msgid = msgid;

	timeout_remove(&conn->to);
	conn->to = timeout_add(DB_LDAP_REQUEST_LOST_TIMEOUT_SECS*1000,
			       ldap_connection_timeout, conn);
	return 0;
}

static int db_ldap_get_fd(struct ldap_connection *conn)
{
	struct sieve_storage *storage = &conn->lstorage->storage;
	int ret;

	/* Get the connection's fd */
	ret = ldap_get_option(conn->ld, LDAP_OPT_DESC, &conn->fd);
	if (ret != LDAP_SUCCESS) {
		e_error(storage->event, "db: Can't get connection fd: %s",
			ldap_err2string(ret));
		return -1;
	}
	if (conn->fd <= STDERR_FILENO) {
		/* Solaris LDAP library seems to be broken */
		e_error(storage->event,
			"db: Buggy LDAP library returned wrong fd: %d",
			conn->fd);
		return -1;
	}
	i_assert(conn->fd != -1);
	net_set_nonblock(conn->fd, TRUE);
	return 0;
}

static int
db_ldap_set_opt(struct ldap_connection *conn, int opt, const void *value,
		const char *optname, const char *value_str)
{
	struct sieve_storage *storage = &conn->lstorage->storage;
	int ret;

	ret = ldap_set_option(conn->ld, opt, value);
	if (ret != LDAP_SUCCESS) {
		e_error(storage->event, "db: Can't set option %s to %s: %s",
			optname, value_str, ldap_err2string(ret));
		return -1;
	}
	return 0;
}

static int
db_ldap_set_opt_str(struct ldap_connection *conn, int opt, const char *value,
		    const char *optname)
{
	if (value != NULL)
		return db_ldap_set_opt(conn, opt, value, optname, value);
	return 0;
}

static int db_ldap_set_tls_options(struct ldap_connection *conn)
{
	const struct sieve_ldap_settings *set = conn->lstorage->ldap_set;

	if (!set->starttls)
		return 0;

#ifdef OPENLDAP_TLS_OPTIONS
	if (db_ldap_set_opt_str(conn, LDAP_OPT_X_TLS_CACERTFILE,
				set->tls_ca_cert_file, "tls_ca_cert_file") < 0)
		return -1;
	if (db_ldap_set_opt_str(conn, LDAP_OPT_X_TLS_CACERTDIR,
				set->tls_ca_cert_dir, "tls_ca_cert_dir") < 0)
		return -1;
	if (db_ldap_set_opt_str(conn, LDAP_OPT_X_TLS_CERTFILE,
				set->tls_cert_file, "tls_cert_file") < 0)
		return -1;
	if (db_ldap_set_opt_str(conn, LDAP_OPT_X_TLS_KEYFILE,
				set->tls_key_file, "tls_key_file") < 0)
		return -1;
	if (db_ldap_set_opt_str(conn, LDAP_OPT_X_TLS_CIPHER_SUITE,
				set->tls_cipher_suite, "tls_cipher_suite") < 0)
		return -1;
	if (*set->tls_require_cert != '\0') {
		if (db_ldap_set_opt(conn, LDAP_OPT_X_TLS_REQUIRE_CERT,
				    &set->parsed.tls_require_cert,
				    "tls_require_cert",
				    set->tls_require_cert) < 0)
			return -1;
	}
#else
	if (*set->tls_ca_cert_file != '\0' ||
	    *set->tls_ca_cert_dir != '\0' ||
	    *set->tls_cert_file != '\0' ||
	    *set->tls_key_file != '\0' ||
	    *set->tls_cipher_suite != '\0') {
		e_warning(&conn->lstorage->storage, "db: "
			  "tls_* settings ignored, "
			  "your LDAP library doesn't seem to support them");
	}
#endif
	return 0;
}

static int db_ldap_set_options(struct ldap_connection *conn)
{
	const struct sieve_ldap_settings *set = conn->lstorage->ldap_set;
	struct sieve_storage *storage = &conn->lstorage->storage;
	unsigned int ldap_version;
	int value;

	if (db_ldap_set_opt(conn, LDAP_OPT_DEREF, &set->parsed.deref,
			    "deref", set->deref) < 0)
		return -1;
#ifdef LDAP_OPT_DEBUG_LEVEL
	if (set->debug_level != 0) {
		if (db_ldap_set_opt(conn, LDAP_OPT_DEBUG_LEVEL, &value,
				    "debug_level", dec2str(set->debug_level)) < 0)
			return -1;
	}
#endif

	if (set->version < 3) {
		if (!array_is_empty(&set->auth_sasl_mechanisms)) {
			e_error(storage->event,
				"db: ldap_auth_sasl_mechanisms requires ldap_version=3");
			return -1;
		}
		if (set->starttls) {
			e_error(storage->event,
				"db: ldap_starttls=yes requires ldap_version=3");
			return -1;
		}
	}

	ldap_version = set->version;
	if (db_ldap_set_opt(conn, LDAP_OPT_PROTOCOL_VERSION, &ldap_version,
			"protocol_version", dec2str(ldap_version)) < 0)
		return -1;
	if (db_ldap_set_tls_options(conn) < 0)
		return -1;
	return 0;
}

int sieve_ldap_db_connect(struct ldap_connection *conn)
{
	const struct sieve_ldap_settings *set = conn->lstorage->ldap_set;
	struct sieve_storage *storage = &conn->lstorage->storage;
	struct timeval start, end;
	bool debug;
#if defined(HAVE_LDAP_SASL) || defined(LDAP_HAVE_START_TLS_S)
	int ret;
#endif

	if (conn->conn_state != LDAP_CONN_STATE_DISCONNECTED)
		return 0;

	debug = set->debug_level > 0;

	if (debug)
		i_gettimeofday(&start);
	i_assert(conn->pending_count == 0);
	if (conn->ld == NULL) {
		if (ldap_initialize(&conn->ld, set->uris) != LDAP_SUCCESS) {
			e_error(storage->event, "db: "
				"ldap_init() failed with uris: %s",
				set->uris);
			return -1;
		}

		if (db_ldap_set_options(conn) < 0)
			return -1;
	}

	if (set->starttls) {
#ifdef LDAP_HAVE_START_TLS_S
		ret = ldap_start_tls_s(conn->ld, NULL, NULL);
		if (ret != LDAP_SUCCESS) {
			if (ret == LDAP_OPERATIONS_ERROR &&
			    *set->uris != '\0' &&
			    str_begins_with(set->uris, "ldaps:")) {
				e_error(storage->event, "db: "
					"Don't use both ldap_starttls=yes and ldaps URI");
			}
			e_error(storage->event, "db: "
				"ldap_start_tls_s() failed: %s",
				ldap_err2string(ret));
			return -1;
		}
#else
		e_error(storage->event, "db: "
			"Your LDAP library doesn't support TLS");
		return -1;
#endif
	}

	if (!array_is_empty(&set->auth_sasl_mechanisms)) {
#ifdef HAVE_LDAP_SASL
		struct db_ldap_sasl_bind_context context;

		i_zero(&context);
		context.authcid = set->auth_dn;
		context.passwd = set->auth_dn_password;
		context.realm = set->auth_sasl_realm;
		context.authzid = set->auth_sasl_authz_id;

		const char *mechs = t_array_const_string_join(
			&set->auth_sasl_mechanisms, " ");

		/* There doesn't seem to be a way to do SASL binding
		   asynchronously.. */
		ret = ldap_sasl_interactive_bind_s(conn->ld, NULL, mechs,
						   NULL, NULL, LDAP_SASL_QUIET,
						   sasl_interact, &context);
		if (db_ldap_connect_finish(conn, ret) < 0)
			return -1;
#else
		e_error(storage->event, "db: "
			"ldap_auth_sasl_mechanisms is set, but no SASL support compiled in");
		return -1;
#endif
		conn->conn_state = LDAP_CONN_STATE_BOUND;
	} else {
		if (db_ldap_bind(conn) < 0)
			return -1;
	}
	if (debug) {
		i_gettimeofday(&end);
		long long msecs = timeval_diff_msecs(&end, &start);
		e_debug(storage->event, "db: "
			"Initialization took %lld msecs", msecs);
	}

	if (db_ldap_get_fd(conn) < 0)
		return -1;
	conn->io = io_add(conn->fd, IO_READ, ldap_input, conn);
	return 0;
}

void db_ldap_enable_input(struct ldap_connection *conn, bool enable)
{
	if (!enable) {
		io_remove(&conn->io);
	} else {
		if (conn->io == NULL && conn->fd != -1) {
			conn->io = io_add(conn->fd, IO_READ, ldap_input, conn);
			ldap_input(conn);
		}
	}
}

static void db_ldap_disconnect_timeout(struct ldap_connection *conn)
{
	db_ldap_abort_requests(conn, UINT_MAX,
		DB_LDAP_REQUEST_DISCONNECT_TIMEOUT_SECS, FALSE,
		"Aborting (timeout), we're not connected to LDAP server");

	if (aqueue_count(conn->request_queue) == 0) {
		/* no requests left, remove this timeout handler */
		timeout_remove(&conn->to);
	}
}

static void db_ldap_conn_close(struct ldap_connection *conn)
{
	struct ldap_request *const *requests, *request;
	unsigned int i;

	conn->conn_state = LDAP_CONN_STATE_DISCONNECTED;
	conn->default_bind_msgid = -1;

	timeout_remove(&conn->to);

	if (conn->pending_count != 0) {
		requests = array_idx(&conn->request_array, 0);
		for (i = 0; i < conn->pending_count; i++) {
			request = requests[aqueue_idx(conn->request_queue, i)];

			i_assert(request->msgid != -1);
			request->msgid = -1;
		}
		conn->pending_count = 0;
	}

	if (conn->ld != NULL) {
		ldap_unbind(conn->ld);
		conn->ld = NULL;
	}
	conn->fd = -1;

	/* The fd may have already been closed before ldap_unbind(), so we'll
	   have to use io_remove_closed(). */
	io_remove_closed(&conn->io);

	if (aqueue_count(conn->request_queue) > 0) {
		conn->to = timeout_add(
			DB_LDAP_REQUEST_DISCONNECT_TIMEOUT_SECS * 1000/2,
			db_ldap_disconnect_timeout, conn);
	}
}

struct ldap_field_find_context {
	ARRAY_TYPE(string) attr_names;
	pool_t pool;
};

#define IS_LDAP_ESCAPED_CHAR(c) \
	((c) == '*' || (c) == '(' || (c) == ')' || (c) == '\\')

const char *ldap_escape(const char *str)
{
	const char *p;
	string_t *ret;

	for (p = str; *p != '\0'; p++) {
		if (IS_LDAP_ESCAPED_CHAR(*p))
			break;
	}

	if (*p == '\0')
		return str;

	ret = t_str_new((size_t) (p - str) + 64);
	str_append_data(ret, str, (size_t) (p - str));

	for (; *p != '\0'; p++) {
		if (IS_LDAP_ESCAPED_CHAR(*p))
			str_append_c(ret, '\\');
		str_append_c(ret, *p);
	}
	return str_c(ret);
}

struct ldap_connection *sieve_ldap_db_init(struct sieve_ldap_storage *lstorage)
{
	struct ldap_connection *conn;
	pool_t pool;

	pool = pool_alloconly_create("ldap_connection", 1024);
	conn = p_new(pool, struct ldap_connection, 1);
	conn->pool = pool;
	conn->refcount = 1;
	conn->lstorage = lstorage;

	conn->conn_state = LDAP_CONN_STATE_DISCONNECTED;
	conn->default_bind_msgid = -1;
	conn->fd = -1;

	i_array_init(&conn->request_array, 512);
	conn->request_queue = aqueue_init(&conn->request_array.arr);

	conn->next = ldap_connections;
	ldap_connections = conn;
	return conn;
}

void sieve_ldap_db_unref(struct ldap_connection **_conn)
{
	struct ldap_connection *conn = *_conn;
	struct ldap_connection **p;

	if (conn == NULL)
		return;
	*_conn = NULL;

	i_assert(conn->refcount >= 0);
	if (--conn->refcount > 0)
		return;

	for (p = &ldap_connections; *p != NULL; p = &(*p)->next) {
		if (*p == conn) {
			*p = conn->next;
			break;
		}
	}

	db_ldap_abort_requests(conn, UINT_MAX, 0, FALSE, "Shutting down");
	i_assert(conn->pending_count == 0);
	db_ldap_conn_close(conn);
	i_assert(conn->to == NULL);

	array_free(&conn->request_array);
	aqueue_deinit(&conn->request_queue);

	pool_unref(&conn->pool);
}

static void db_ldap_switch_ioloop(struct ldap_connection *conn)
{
	if (conn->to != NULL)
		conn->to = io_loop_move_timeout(&conn->to);
	if (conn->io != NULL)
		conn->io = io_loop_move_io(&conn->io);
}

static void db_ldap_wait(struct ldap_connection *conn)
{
	struct sieve_storage *storage = &conn->lstorage->storage;
	struct ioloop *prev_ioloop = current_ioloop;

	i_assert(conn->ioloop == NULL);

	if (aqueue_count(conn->request_queue) == 0)
		return;

	conn->ioloop = io_loop_create();
	db_ldap_switch_ioloop(conn);
	/* Either we're waiting for network I/O or we're getting out of a
	   callback using timeout_add_short(0) */
	i_assert(io_loop_have_ios(conn->ioloop) ||
		 io_loop_have_immediate_timeouts(conn->ioloop));

	do {
		e_debug(storage->event, "db: "
			"Waiting for %d requests to finish",
			aqueue_count(conn->request_queue));
		io_loop_run(conn->ioloop);
	} while (aqueue_count(conn->request_queue) > 0);

	e_debug(storage->event, "db: All requests finished");

	current_ioloop = prev_ioloop;
	db_ldap_switch_ioloop(conn);
	current_ioloop = conn->ioloop;
	io_loop_destroy(&conn->ioloop);
}

static void sieve_ldap_db_script_free(unsigned char *script)
{
	i_free(script);
}

static int
sieve_ldap_db_get_script_modattr(struct ldap_connection *conn,
				 LDAPMessage *entry, pool_t pool,
				 const char **modattr_r)
{
	const struct sieve_ldap_storage_settings *set = conn->lstorage->set;
	struct sieve_storage *storage = &conn->lstorage->storage;
	char *attr, **vals;
	BerElement *ber;

	*modattr_r = NULL;

	attr = ldap_first_attribute(conn->ld, entry, &ber);
	while (attr != NULL) {
		if (strcmp(attr, set->mod_attr) == 0) {
			vals = ldap_get_values(conn->ld, entry, attr);
			if (vals == NULL || vals[0] == NULL)
				return 0;

			if (vals[1] != NULL) {
				e_warning(storage->event, "db: "
					  "Search returned more than one Sieve modified attribute '%s'; "
					  "using only the first one.",
					  set->mod_attr);
			}

			*modattr_r = p_strdup(pool, vals[0]);

			ldap_value_free(vals);
			ldap_memfree(attr);
			return 1;
		}
		ldap_memfree(attr);
		attr = ldap_next_attribute(conn->ld, entry, ber);
	}
	ber_free(ber, 0);

	return 0;
}

static int
sieve_ldap_db_get_script(struct ldap_connection *conn, LDAPMessage *entry,
			 struct istream **script_r)
{
	const struct sieve_ldap_storage_settings *set = conn->lstorage->set;
	struct sieve_storage *storage = &conn->lstorage->storage;
	char *attr;
	unsigned char *data;
	size_t size;
	struct berval **vals;
	BerElement *ber;

	attr = ldap_first_attribute(conn->ld, entry, &ber);
	while (attr != NULL) {
		if (strcmp(attr, set->script_attr) == 0) {
			vals = ldap_get_values_len(conn->ld, entry, attr);
			if (vals == NULL || vals[0] == NULL)
				return 0;

			if (vals[1] != NULL) {
				e_warning(storage->event, "db: "
					  "Search returned more than one Sieve script attribute '%s'; "
					  "using only the first one.",
					  set->script_attr);
			}

			size = vals[0]->bv_len;
			data = i_malloc(size);

			e_debug(storage->event, "db: "
				"Found script with length %zu", size);

			memcpy(data, vals[0]->bv_val, size);

			ldap_value_free_len(vals);
			ldap_memfree(attr);

			*script_r = i_stream_create_from_data(data, size);
			i_stream_add_destroy_callback(
				*script_r, sieve_ldap_db_script_free, data);
			return 1;
		}
		ldap_memfree(attr);
		attr = ldap_next_attribute(conn->ld, entry, ber);
	}
	ber_free(ber, 0);

	return 0;
}

const struct var_expand_table
auth_request_var_expand_static_tab[] = {
	{ .key = "user", .value = NULL },
	{ .key = "username", .value = NULL },
	{ .key = "domain", .value = NULL },
	{ .key = "home", .value = NULL },
	{ .key = "name", .value = NULL },
	VAR_EXPAND_TABLE_END
};

static const struct var_expand_table *
db_ldap_get_var_expand_table(struct ldap_connection *conn, const char *name)
{
	struct sieve_ldap_storage *lstorage = conn->lstorage;
	struct sieve_instance *svinst = lstorage->storage.svinst;
	const unsigned int auth_count =
		N_ELEMENTS(auth_request_var_expand_static_tab);
	struct var_expand_table *tab;

	/* Keep the extra fields at the beginning. the last static_tab field
	   contains the ending NULL-fields. */
	tab = t_malloc_no0((auth_count) * sizeof(*tab));

	memcpy(tab, auth_request_var_expand_static_tab,
	       auth_count * sizeof(*tab));

	tab[0].value = ldap_escape(svinst->username);
	tab[1].value = ldap_escape(t_strcut(svinst->username, '@'));
	tab[2].value = strchr(svinst->username, '@');
	if (tab[2].value != NULL)
		tab[2].value = ldap_escape(tab[2].value+1);
	tab[3].value = ldap_escape(svinst->home_dir);
	tab[4].value = ldap_escape(name);
	return tab;
}

struct sieve_ldap_script_lookup_request {
	struct ldap_request request;

	unsigned int entries;
	const char *result_dn;
	const char *result_modattr;
};

static void
sieve_ldap_lookup_script_callback(struct ldap_connection *conn,
				  struct ldap_request *request,
				  LDAPMessage *res)
{
	struct sieve_storage *storage = &conn->lstorage->storage;
	struct sieve_ldap_script_lookup_request *srequest =
		container_of(request, struct sieve_ldap_script_lookup_request,
			     request);

	if (res == NULL) {
		io_loop_stop(conn->ioloop);
		return;
	}

	if (ldap_msgtype(res) != LDAP_RES_SEARCH_RESULT) {
		if (srequest->result_dn == NULL) {
			srequest->result_dn = p_strdup(
				request->pool, ldap_get_dn(conn->ld, res));
			(void)sieve_ldap_db_get_script_modattr(
				conn, res, request->pool,
				&srequest->result_modattr);
		} else if (srequest->entries++ == 0) {
			e_warning(storage->event, "db: "
				  "Search returned more than one entry for Sieve script; "
				  "using only the first one.");
		}
	} else {
		io_loop_stop(conn->ioloop);
		return;
	}
}

int sieve_ldap_db_lookup_script(struct ldap_connection *conn, const char *name,
				const char **dn_r, const char **modattr_r)
{
	struct sieve_ldap_storage *lstorage = conn->lstorage;
	struct sieve_storage *storage = &lstorage->storage;
	const struct sieve_ldap_settings *ldap_set = lstorage->ldap_set;
	const struct sieve_ldap_storage_settings *set = lstorage->set;
	struct sieve_ldap_script_lookup_request *request;
	char **attr_names;
	const char *error;
	string_t *str;

	pool_t pool = pool_alloconly_create(
		"sieve_ldap_script_lookup_request", 512);
	request = p_new(pool, struct sieve_ldap_script_lookup_request, 1);
	request->request.pool = pool;

	const struct var_expand_params params = {
		.table = db_ldap_get_var_expand_table(conn, name),
	};

	str = t_str_new(512);
	if (var_expand(str, ldap_set->base, &params, &error) < 0) {
		e_error(storage->event, "db: "
			"Failed to expand base=%s: %s",
			ldap_set->base, error);
		return -1;
	}
	request->request.base = p_strdup(pool, str_c(str));

	attr_names = p_new(pool, char *, 3);
	attr_names[0] = p_strdup(pool, set->mod_attr);

	str_truncate(str, 0);
	if (var_expand(str, set->filter, &params, &error) < 0) {
		e_error(storage->event, "db: "
			"Failed to expand sieve_ldap_filter=%s: %s",
			set->filter, error);
		return -1;
	}

	request->request.scope = ldap_set->parsed.scope;
	request->request.filter = p_strdup(pool, str_c(str));
	request->request.attributes = attr_names;

	e_debug(storage->event, "base=%s scope=%s filter=%s fields=%s",
		request->request.base, ldap_set->scope,
		request->request.filter,
		t_strarray_join((const char **)attr_names, ","));

	request->request.callback = sieve_ldap_lookup_script_callback;
	db_ldap_request(conn, &request->request);
	db_ldap_wait(conn);

	*dn_r = t_strdup(request->result_dn);
	*modattr_r = t_strdup(request->result_modattr);
	pool_unref(&request->request.pool);
	return (*dn_r == NULL ? 0 : 1);
}

struct sieve_ldap_script_read_request {
	struct ldap_request request;

	unsigned int entries;
	struct istream *result;
};

static void
sieve_ldap_read_script_callback(struct ldap_connection *conn,
				struct ldap_request *request, LDAPMessage *res)
{
	struct sieve_storage *storage = &conn->lstorage->storage;
	struct sieve_ldap_script_read_request *srequest =
		container_of(request, struct sieve_ldap_script_read_request,
			     request);

	if (res == NULL) {
		io_loop_stop(conn->ioloop);
		return;
	}

	if (ldap_msgtype(res) != LDAP_RES_SEARCH_RESULT) {
		if (srequest->result == NULL) {
			(void)sieve_ldap_db_get_script(conn, res,
						       &srequest->result);
		} else {
			e_error(storage->event, "db: "
				"Search returned more than one entry for Sieve script DN");
			i_stream_unref(&srequest->result);
		}

	} else {
		io_loop_stop(conn->ioloop);
		return;
	}
}

int sieve_ldap_db_read_script(struct ldap_connection *conn,
			      const char *dn, struct istream **script_r)
{
	struct sieve_ldap_storage *lstorage = conn->lstorage;
	struct sieve_storage *storage = &lstorage->storage;
	const struct sieve_ldap_storage_settings *set = lstorage->set;
	struct sieve_ldap_script_read_request *request;
	char **attr_names;

	pool_t pool = pool_alloconly_create(
		"sieve_ldap_script_read_request", 512);
	request = p_new(pool, struct sieve_ldap_script_read_request, 1);
	request->request.pool = pool;
	request->request.base = p_strdup(pool, dn);

	attr_names = p_new(pool, char *, 3);
	attr_names[0] = p_strdup(pool, set->script_attr);

	request->request.scope = LDAP_SCOPE_BASE;
	request->request.filter = "(objectClass=*)";
	request->request.attributes = attr_names;

	e_debug(storage->event, "base=%s scope=base filter=%s fields=%s",
		request->request.base, request->request.filter,
		t_strarray_join((const char **)attr_names, ","));

	request->request.callback = sieve_ldap_read_script_callback;
	db_ldap_request(conn, &request->request);
	db_ldap_wait(conn);

	*script_r = request->result;
	pool_unref(&request->request.pool);
	return (*script_r == NULL ? 0 : 1);
}

#endif
