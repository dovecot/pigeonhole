/* Copyright (c) 2025 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"
#include "str-sanitize.h"
#include "array.h"
#include "bsearch-insert-pos.h"
#include "ioloop.h"
#include "dict.h"
#include "settings.h"
#include "mail-storage.h"

#include "sieve-common.h"
#include "sieve-settings.h"
#include "sieve-stringlist.h"
#include "sieve-ast.h"
#include "sieve-code.h"
#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "ext-extlists-settings.h"
#include "ext-extlists-common.h"

/*
 * List match type operand
 */

static const struct sieve_extension_objects ext_match_types =
	SIEVE_EXT_DEFINE_MATCH_TYPE(list_match_type);

const struct sieve_operand_def list_match_type_operand = {
	.name = "list match",
	.ext_def = &extlists_extension,
	.class = &sieve_match_type_operand_class,
	.interface = &ext_match_types
};

/*
 * Configuration
 */

static int
ext_extlists_list_find(struct ext_extlists_context *extctx, const char *name,
		       struct ext_extlists_list **list_r)
{
	int ret;
	const char *error;

	if (extctx == NULL)
		return 0;

	ret = ext_extlists_name_normalize(&name, &error);
	if (ret < 0)
		return -1;

	struct ext_extlists_list *list;

	array_foreach_modifiable(&extctx->lists, list) {
		if (strcasecmp(name, list->set->parsed.name) == 0) {
			*list_r = list;
			return 1;
		}
	}

	return 0;
}

static int
ext_extlists_list_add(struct sieve_instance *svinst,
		      struct ext_extlists_context *extctx,
		      const char *name)
{
	struct ext_extlists_list *list;
	const struct ext_extlists_list_settings *set;
	const char *error;
	int ret;

	ret = ext_extlists_list_find(extctx, name, &list);
	i_assert(ret >= 0);
	if (ret > 0) {
		e_error(svinst->event,
			"extlists: Duplicate list definition with name '%s'",
			name);
		return -1;
	}

	if (settings_get_filter(svinst->event, "sieve_extlists_list", name,
				&ext_extlists_list_setting_parser_info, 0,
				&set, &error) < 0) {
		e_error(svinst->event, "%s", error);
		return -1;
	}

	list = array_append_space(&extctx->lists);
	list->set = set;

	return 0;
}

static void
ext_extlists_list_add_default(struct ext_extlists_context *extctx)
{
	struct ext_extlists_list *list;
	int ret;

	ret = ext_extlists_list_find(extctx, SIEVE_URN_ADDRBOOK_DEFAULT, &list);
	i_assert(ret >= 0);
	if (ret > 0)
		return;

	struct ext_extlists_list_settings *set;
	pool_t pool;

	pool = pool_alloconly_create("sieve extlists default list", 256);
	set = settings_defaults_dup(
		pool, &ext_extlists_list_setting_parser_info);
	set->name = SIEVE_URN_ADDRBOOK_DEFAULT;
	set->parsed.name = SIEVE_URN_ADDRBOOK_DEFAULT;

	list = array_append_space(&extctx->lists);
	list->set = set;
}

static int
ext_extlists_config_lists(struct sieve_instance *svinst,
			  struct ext_extlists_context *extctx)
{
	const char *name;

	if (!array_is_created(&extctx->set->lists))
		return 0;

	array_foreach_elem(&extctx->set->lists, name) {
		if (ext_extlists_list_add(svinst, extctx, name) < 0)
			return -1;
	}
	return 0;
}

int ext_extlists_load(const struct sieve_extension *ext, void **context_r)
{
	struct sieve_instance *svinst = ext->svinst;
	const struct ext_extlists_settings *set;
	const char *error;

	if (settings_get(svinst->event, &ext_extlists_setting_parser_info, 0,
			 &set, &error) < 0) {
		e_error(svinst->event, "%s", error);
		return -1;
	}

	struct ext_extlists_context *extctx;
	unsigned int lists_count = (!array_is_created(&set->lists) ?
				    0 : array_count(&set->lists));

	extctx = i_new(struct ext_extlists_context, 1);
	extctx->set = set;
	i_array_init(&extctx->lists, lists_count);
	*context_r = extctx;

	if (ext_extlists_config_lists(svinst, extctx) < 0) {
		ext_extlists_unload(ext);
		return -1;
	}
	ext_extlists_list_add_default(extctx);
	sieve_extension_capabilities_register(ext, &extlists_capabilities);

	return 0;
}

void ext_extlists_unload(const struct sieve_extension *ext)
{
	struct ext_extlists_context *extctx = ext->context;
	struct ext_extlists_list *list;

	if (extctx == NULL)
		return;

	array_foreach_modifiable(&extctx->lists, list) {
		dict_deinit(&list->dict);
		settings_free(list->set);
		pool_unref(&list->cache_pool);
	}

	settings_free(extctx->set);
	array_free(&extctx->lists);
	i_free(extctx);
}

static const char *
ext_extlists_get_lists_string(const struct sieve_extension *ext)
{
	struct ext_extlists_context *extctx = ext->context;

	if (extctx == NULL || array_count(&extctx->lists) == 0)
		return NULL;

	struct ext_extlists_list *list;
	string_t *result = t_str_new(128);

	array_foreach_modifiable(&extctx->lists, list) {
		if (str_len(result) > 0)
			str_append_c(result, ' ');
		str_append(result, list->set->parsed.name);
	}
	return str_c(result);
}

static int
ext_extlists_list_init(struct ext_extlists_list *list,
		       struct event *event_parent, const char **error_r)
{
	int ret;

	if (list->dict != NULL)
		return 1;

	struct event *event = event_create(event_parent);
	event_add_str(event, "sieve_extlists_list", list->set->name);
	settings_event_add_list_filter_name(event, "sieve_extlists_list",
					    list->set->name);
	ret = dict_init_auto(event, &list->dict, error_r);
	event_unref(&event);

	return ret;
}

static int
ext_extlists_list_cache_cmp(const struct ext_extlists_cache_entry *entry1,
			    const struct ext_extlists_cache_entry *entry2)
{
	return strcmp(entry1->value, entry2->value);
}

static void
ext_extlists_list_cache_add(struct ext_extlists_list *list, const char *value,
			    bool matched)
{
	struct ext_extlists_cache_entry entry;
	unsigned int insert_idx;
	bool found;

	if (list->cache_pool == NULL) {
		list->cache_pool = pool_alloconly_create(
			"sieve extlists list cache", 4096);
		p_array_init(&list->cache, list->cache_pool, 64);
	}

	i_zero(&entry);
	entry.value = value;
	entry.matched = matched;

	found = array_bsearch_insert_pos(&list->cache, &entry,
					 ext_extlists_list_cache_cmp,
					 &insert_idx);
	i_assert(!found);

	entry.value = p_strdup(list->cache_pool, value);
	array_insert(&list->cache, insert_idx, &entry, 1);
}

static bool
ext_extlists_list_cache_lookup(struct ext_extlists_list *list,
			       const char *value, bool *matched_r)
{
	*matched_r = FALSE;

	if (list->cache_pool == NULL)
		return FALSE;

	struct ext_extlists_cache_entry entry;
	unsigned int idx;

	i_zero(&entry);
	entry.value = value;

	if (!array_bsearch_insert_pos(&list->cache, &entry,
				      ext_extlists_list_cache_cmp, &idx))
		return FALSE;

	const struct ext_extlists_cache_entry *found_entry;

	found_entry = array_idx(&list->cache, idx);
	*matched_r = found_entry->matched;
	return TRUE;
}

/*
 * Extlists capability
 */

const struct sieve_extension_capabilities extlists_capabilities = {
	"extlists",
	ext_extlists_get_lists_string,
};

/*
 * Runtime operand checking
 */

int ext_extlists_runtime_ext_list_validate(const struct sieve_runtime_env *renv,
					   string_t *ext_list_name)
{
	const struct sieve_extension *this_ext = renv->oprtn->ext;
	struct ext_extlists_context *extctx = this_ext->context;
	const char *list_name = str_c(ext_list_name);
	struct ext_extlists_list *list;

	return ext_extlists_list_find(extctx, list_name, &list);
}

/*
 * Lookup
 */

#define DICT_LOOKUP_BATCH_MAX        100
#define DICT_LOOKUP_BATCH_MIN        100

#define DICT_SIEVE_PATH              DICT_PATH_PRIVATE"sieve/"
#define DICT_EXTLISTS_PATH           DICT_SIEVE_PATH"extlists/"

struct _dict_lookup;
struct _dict_lookup_value;
struct _dict_lookup_list;
struct _dict_lookup_context;

struct _dict_lookup {
	struct _dict_lookup_value *value;
	struct ext_extlists_list *list;
};

struct _dict_lookup_value {
	struct _dict_lookup_context *context;
	unsigned int id;
	char *value;

	unsigned int lookups_pending;
};

struct _dict_lookup_list {
	struct _dict_lookup_context *context;

	struct ext_extlists_list *list;
};

struct _dict_lookup_context {
	pool_t pool;
	const struct sieve_runtime_env *renv;
	struct ext_extlists_context *extctx;

	ARRAY(struct _dict_lookup_list) lists;

	struct ioloop *ioloop;

	struct sieve_stringlist *values;

	unsigned int lookup_id_counter;
	unsigned int batch_max, lookups_max;

	ARRAY(struct _dict_lookup) lookups;
	ARRAY(struct _dict_lookup_value) lookup_values;
	unsigned int lookups_pending;

	char *match;
	int status;

	bool warned:1;
	bool return_match:1;
	bool found:1;
	bool lookup_continuing:1;
	bool lookup_finished:1;
};

static void _dict_lookup_continue(struct _dict_lookup_context *dlctx);

static int
_dict_lookup_list_init(struct _dict_lookup_context *dlctx, string_t *key_item)
{
	const struct sieve_runtime_env *renv = dlctx->renv;
	const struct sieve_execute_env *eenv = renv->exec_env;
	struct ext_extlists_context *extctx = dlctx->extctx;
	const char *key = str_c(key_item);
	struct ext_extlists_list *list;
	struct _dict_lookup_list *dllst;
	const char *error;
	int ret;

	if (strlen(key) != str_len(key_item)) {
		if (dlctx->warned)
			return SIEVE_EXEC_OK;
		dlctx->warned = TRUE;

		sieve_runtime_error(
			renv, NULL,
			"Key item for \":list\" match contains NUL byte");
		return SIEVE_EXEC_OK;
	}
	ret = ext_extlists_list_find(extctx, key, &list);
	if (ret < 0) {
		if (dlctx->warned)
			return SIEVE_EXEC_OK;
		dlctx->warned = TRUE;

		sieve_runtime_warning(
			renv, NULL,
			"Key item '%s' for \":list\" match is not a valid list name",
			str_sanitize_utf8(key, 1024));
		return SIEVE_EXEC_OK;
	}
	if (ret == 0) {
		if (dlctx->warned)
			return SIEVE_EXEC_OK;
		dlctx->warned = TRUE;

		sieve_runtime_warning(
			renv, NULL,
			"Key item '%s' for \":list\" match is not a known list name",
			str_sanitize_utf8(key, 1024));
		return SIEVE_EXEC_OK;
	}

	ret = ext_extlists_list_init(list, eenv->event, &error);
	if (ret < 0) {
		sieve_runtime_critical(
			renv, NULL, "\":list\" match",
			"\":list\" match: Failed to initialize dict: %s",
			error);
		return SIEVE_EXEC_FAILURE;
	}
	if (ret == 0) {
		sieve_runtime_debug(
			renv, NULL,
			"Key item '%s' for \":list\" match yields empty list",
			str_sanitize_utf8(key, 1024));
		return SIEVE_EXEC_OK;
	}

	dllst = array_append_space(&dlctx->lists);
	dllst->list = list;

	if (list->dict != NULL)
		dict_switch_ioloop(list->dict);

	return SIEVE_EXEC_OK;
}

static void
_dict_lookup_callback(const struct dict_lookup_result *result,
		      struct _dict_lookup *dl)
{
	struct _dict_lookup_value *dlval = dl->value;
	struct _dict_lookup_context *dlctx = dlval->context;
	const struct sieve_runtime_env *renv = dlctx->renv;

	i_assert(dlctx->lookups_pending > 0);
	dlval->lookups_pending--;
	dlctx->lookups_pending--;

	if (result->ret < 0) {
		if (dlctx->lookup_finished)
			return;
		sieve_runtime_critical(
			renv, NULL, "\":list\" match",
			"\":list\" match: "
			"Failed to lookup value '%s' from list '%s' "
			"with dict error: %s",
			str_sanitize(dlval->value, 256),
			dl->list->set->parsed.name, result->error);
		sieve_runtime_trace(
			renv, SIEVE_TRLVL_MATCHING,
			"extlists lookup[%u] in list '%s' failed",
			dlval->id, dl->list->set->parsed.name);
		dlctx->status = SIEVE_EXEC_TEMP_FAILURE;
		dlctx->lookup_finished = TRUE;
		io_loop_stop(dlctx->ioloop);
		return;
	}

	if (result->ret > 0) {
		sieve_runtime_trace(
			renv, SIEVE_TRLVL_MATCHING,
			"extlists lookup[%u] in list '%s' yielded result",
			dlval->id, dl->list->set->parsed.name);
		ext_extlists_list_cache_add(dl->list, dlval->value, TRUE);
		dlctx->found = TRUE;
		dlctx->match = dlval->value;
		dlctx->lookup_finished = TRUE;
		io_loop_stop(dlctx->ioloop);
		return;
	}

	sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
			    "extlists lookup[%u] in list '%s' yielded no result",
			    dlval->id, dl->list->set->parsed.name);
	ext_extlists_list_cache_add(dl->list, dlval->value, FALSE);

	if (dlval->lookups_pending == 0) {
		if (dlctx->match != dlval->value)
			i_free(dlval->value);
		i_zero(dlval);
		_dict_lookup_continue(dlctx);
	}
}

static int
_dict_lookup_next_value(struct _dict_lookup_context *dlctx,
			const struct dict_op_settings *set, unsigned int index)
{
	const struct sieve_runtime_env *renv = dlctx->renv;
	struct _dict_lookup_value *dlval;
	int ret;

	dlval = array_idx_get_space(&dlctx->lookup_values, index);
	if (dlval->context != NULL)
		return 0;

	string_t *value_item = NULL;

	ret = sieve_stringlist_next_item(dlctx->values, &value_item);
	if (ret == 0)
		return 0;
	if (ret < 0) {
		dlctx->status = dlctx->values->exec_status;
		return -1;
	}

	dlval->id = dlctx->lookup_id_counter++;
	dlval->context = dlctx;
	dlval->value = i_strdup(str_c(value_item));

	sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
			    "extlists lookup[%u] for '%s'", dlval->id,
			    str_sanitize(dlval->value, 256));

	const char *dict_path;

	dict_path = t_strconcat(DICT_EXTLISTS_PATH,
				dict_escape_string(str_c(value_item)), NULL);

	const struct _dict_lookup_list *dllst;
	unsigned int lists_count = array_count(&dlctx->lists);

	dlval->lookups_pending++; /* lock dlval for immediate callbacks */
	array_foreach(&dlctx->lists, dllst) {
		struct ext_extlists_list *list = dllst->list;
		struct _dict_lookup *dl;

		dl = array_idx_get_space(&dlctx->lookups,
					 (index * lists_count +
					  array_foreach_idx(&dlctx->lists,
							    dllst)));
		i_zero(dl);
		dl->value = dlval;
		dl->list = dllst->list;

		if (list->dict == NULL) {
			struct dict_lookup_result result;

			dlval->lookups_pending++;
			dlctx->lookups_pending++;

			i_zero(&result);
			_dict_lookup_callback(&result, dl);
			continue;
		}
		if (str_len(value_item) > list->set->max_lookup_size) {
			sieve_runtime_trace(
				renv, SIEVE_TRLVL_MATCHING,
				"skipping extlists lookup[%u] for list '%s': "
				"value is excessively large"
				"(size %zu > %zu bytes)",
				dlval->id, list->set->parsed.name,
				str_len(value_item),
				list->set->max_lookup_size);
			continue;
		}

		bool matched = FALSE;

		if (ext_extlists_list_cache_lookup(list, dlval->value,
						   &matched)) {
			sieve_runtime_trace(
				renv, SIEVE_TRLVL_MATCHING,
				"cache hit for extlists lookup[%u] "
				"(mathed=%s)",
				dlval->id, (matched ? "yes" : "no"));
			if (matched) {
				dlctx->found = TRUE;
				dlctx->match = dlval->value;
				dlval->value = NULL;
				dlctx->lookup_finished = TRUE;
				io_loop_stop(dlctx->ioloop);
				break;
			}
			continue;
		}

		dlval->lookups_pending++;
		dlctx->lookups_pending++;

		dict_lookup_async(list->dict, set, dict_path,
				  _dict_lookup_callback, dl);
		if (dlctx->lookup_finished)
			break;
	}

	if (--dlval->lookups_pending == 0) {
		if (dlctx->match != dlval->value)
			i_free(dlval->value);
		i_zero(dlval);
		_dict_lookup_continue(dlctx);
	}

	return 1;
}

static void _dict_lookup_continue(struct _dict_lookup_context *dlctx)
{
	const struct sieve_runtime_env *renv = dlctx->renv;
	unsigned int i;

	if (dlctx->lookup_finished)
		return;
	if (dlctx->lookups_pending >= DICT_LOOKUP_BATCH_MIN)
		return;
	if (dlctx->lookup_continuing)
		return;

	const struct dict_op_settings *set = mail_user_get_dict_op_settings(
		renv->exec_env->scriptenv->user);
	int ret = 1;

	dlctx->lookup_continuing = TRUE;
	while (ret > 0 && !dlctx->lookup_finished &&
	       dlctx->lookups_pending < dlctx->lookups_max) {
		for (i = 0; i < dlctx->lookups_max && !dlctx->lookup_finished;
		     i++) {
			ret = _dict_lookup_next_value(dlctx, set, i);
			if (ret <= 0)
				break;
		}
	}
	dlctx->lookup_continuing = FALSE;

	if (ret == 0 && dlctx->lookups_pending == 0) {
		sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
				    "all lookups finished");
		dlctx->lookup_finished = TRUE;
		io_loop_stop(dlctx->ioloop);
	}
}

static void
ext_extlists_do_lookup(struct _dict_lookup_context *dlctx,
		       struct sieve_stringlist *key_list)
{
	const struct sieve_runtime_env *renv = dlctx->renv;
	string_t *key_item = NULL;
	int ret;

	while ((ret = sieve_stringlist_next_item(key_list, &key_item)) > 0 ) {
		ret = _dict_lookup_list_init(dlctx, key_item);
		if (ret != SIEVE_EXEC_OK) {
			dlctx->status = ret;
			return;
		}
	}
	if (ret < 0) {
		dlctx->status = key_list->exec_status;
		return;
	}

	if (array_count(&dlctx->lists) == 0) {
		sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
				    "keylist yielded empty lists");
		dlctx->status = SIEVE_EXEC_OK;
		return;
	}

	unsigned int lists_count = array_count(&dlctx->lists);
	dlctx->batch_max = I_MAX(DICT_LOOKUP_BATCH_MAX, lists_count);
	dlctx->lookups_max = dlctx->batch_max / lists_count;
	dlctx->batch_max = dlctx->lookups_max * lists_count;
	p_array_init(&dlctx->lookup_values, dlctx->pool, dlctx->lookups_max);
	p_array_init(&dlctx->lookups, dlctx->pool,
		     dlctx->lookups_max * lists_count);

	_dict_lookup_continue(dlctx);
	while (!dlctx->lookup_finished)
		io_loop_run(dlctx->ioloop);

	struct _dict_lookup_list *dll;
	struct _dict_lookup_value *dlval;

	array_foreach_modifiable(&dlctx->lists, dll) {
		if (dll->list->dict != NULL)
			dict_wait(dll->list->dict);
	}
	array_foreach_modifiable(&dlctx->lookup_values, dlval) {
		if (dlval->value != dlctx->match)
			i_free(dlval->value);
	}
}

int ext_extlists_lookup(const struct sieve_runtime_env *renv,
			struct ext_extlists_context *extctx,
			struct sieve_stringlist *value_list,
			struct sieve_stringlist *key_list,
			const char **match_r, bool *found_r)
{
	struct _dict_lookup_context dlctx;
	struct ioloop *prev_ioloop;

	*found_r = FALSE;
	if (match_r != NULL)
		*match_r = NULL;

	if (extctx == NULL || array_count(&extctx->lists) == 0) {
		sieve_runtime_trace(renv, SIEVE_TRLVL_MATCHING,
				    "no lists configured");
		return SIEVE_EXEC_OK;
	}

	i_zero(&dlctx);
	dlctx.pool = pool_alloconly_create("sieve extlists lookup", 1024);
	dlctx.renv = renv;
	dlctx.extctx = extctx;
	dlctx.status = SIEVE_EXEC_OK;
	dlctx.return_match = (match_r != NULL);
	p_array_init(&dlctx.lists, dlctx.pool, array_count(&extctx->lists));
	dlctx.values = value_list;

	struct _dict_lookup_list *dll;

	prev_ioloop = current_ioloop;
	i_assert(prev_ioloop != NULL);
	dlctx.ioloop = io_loop_create();

	ext_extlists_do_lookup(&dlctx, key_list);

	io_loop_set_current(prev_ioloop);
	array_foreach_modifiable(&dlctx.lists, dll) {
		if (dll->list->dict != NULL)
			dict_switch_ioloop(dll->list->dict);
	}
	io_loop_set_current(dlctx.ioloop);
	io_loop_destroy(&dlctx.ioloop);

	if (match_r != NULL)
		*match_r = t_strdup(dlctx.match);
	i_free(dlctx.match);

	pool_unref(&dlctx.pool);

	*found_r = dlctx.found;
	return dlctx.status;
}
