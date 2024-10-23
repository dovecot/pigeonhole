#ifndef DB_LDAP_H
#define DB_LDAP_H

/* Functions like ldap_bind() have been deprecated in OpenLDAP 2.3 This define
   enables them until the code here can be refactored. */
#define LDAP_DEPRECATED 1

/* Maximum number of pending requests before delaying new requests. */
#define DB_LDAP_MAX_PENDING_REQUESTS 8
/* If LDAP connection is down, fail requests after waiting for this long. */
#define DB_LDAP_REQUEST_DISCONNECT_TIMEOUT_SECS 4
/* If request is still in queue after this many seconds and other requests have
   been replied, assume the request was lost and abort it. */
#define DB_LDAP_REQUEST_LOST_TIMEOUT_SECS 60
/* If server disconnects us, don't reconnect if no requests have been sent for
   this many seconds. */
#define DB_LDAP_IDLE_RECONNECT_SECS 60

#include <ldap.h>

#define HAVE_LDAP_SASL
#ifdef HAVE_SASL_SASL_H
#  include <sasl/sasl.h>
#elif defined (HAVE_SASL_H)
#  include <sasl.h>
#else
#  undef HAVE_LDAP_SASL
#endif
#ifdef LDAP_OPT_X_TLS
#  define OPENLDAP_TLS_OPTIONS
#endif
#if !defined(SASL_VERSION_MAJOR) || SASL_VERSION_MAJOR < 2
#  undef HAVE_LDAP_SASL
#endif

#ifndef LDAP_SASL_QUIET
#  define LDAP_SASL_QUIET 0 /* Doesn't exist in Solaris LDAP */
#endif

/* Older versions may require calling ldap_result() twice */
#if LDAP_VENDOR_VERSION <= 20112
#  define OPENLDAP_ASYNC_WORKAROUND
#endif

/* Solaris LDAP library doesn't have LDAP_OPT_SUCCESS */
#ifndef LDAP_OPT_SUCCESS
#  define LDAP_OPT_SUCCESS LDAP_SUCCESS
#endif

struct ldap_connection;
struct ldap_request;

typedef void
db_search_callback_t(struct ldap_connection *conn, struct ldap_request *request,
		     LDAPMessage *res);

struct ldap_request {
	pool_t pool;

	/* msgid for sent requests, -1 if not sent */
	int msgid;
	/* timestamp when request was created */
	time_t create_time;

	bool failed;

	db_search_callback_t *callback;

	const char *base;
	const char *filter;
	int scope;
	char **attributes;

	struct db_ldap_result *result;
};

enum ldap_connection_state {
	/* Not connected */
	LDAP_CONN_STATE_DISCONNECTED,
	/* Binding - either to default dn or doing auth bind */
	LDAP_CONN_STATE_BINDING,
	/* Bound */
	LDAP_CONN_STATE_BOUND
};

struct ldap_connection {
	struct ldap_connection *next;

	struct sieve_ldap_storage *lstorage;

	pool_t pool;
	int refcount;

	LDAP *ld;
	enum ldap_connection_state conn_state;
	int default_bind_msgid;

	int fd;
	struct io *io;
	struct timeout *to;
	struct ioloop *ioloop;

	/* Request queue contains sent requests at tail (msgid != -1) and
	   queued requests at head (msgid == -1). */
	struct aqueue *request_queue;
	ARRAY(struct ldap_request *) request_array;
	/* Number of messages in queue with msgid != -1 */
	unsigned int pending_count;

	/* Timestamp when we last received a reply */
	time_t last_reply_stamp;
};

/* Send/queue request */
void db_ldap_request(struct ldap_connection *conn,
		     struct ldap_request *request);

void db_ldap_enable_input(struct ldap_connection *conn, bool enable);

const char *ldap_escape(const char *str);
const char *ldap_get_error(struct ldap_connection *conn);

int sieve_ldap_db_connect(struct ldap_connection *conn);

struct ldap_connection *
sieve_ldap_db_init(struct sieve_ldap_storage *lstorage);
void sieve_ldap_db_unref(struct ldap_connection **conn);

int sieve_ldap_db_lookup_script(struct ldap_connection *conn, const char *name,
				const char **dn_r, const char **modattr_r);
int sieve_ldap_db_read_script(struct ldap_connection *conn, const char *dn,
			      struct istream **script_r);

#endif
