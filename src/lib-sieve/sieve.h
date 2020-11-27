#ifndef SIEVE_H
#define SIEVE_H

struct sieve_script;
struct sieve_binary;

#include "sieve-config.h"
#include "sieve-types.h"
#include "sieve-error.h"

/*
 * Main Sieve library interface
 */

/* Initialize the sieve engine. Must be called before any sieve functionality is
   used. */
struct sieve_instance *
sieve_init(const struct sieve_environment *env,
	   const struct sieve_callbacks *callbacks, void *context, bool debug);

/* Free all memory allocated by the sieve engine. */
void sieve_deinit(struct sieve_instance **_svinst);

/* Get capability string for a particular extension. */
const char *
sieve_get_capabilities(struct sieve_instance *svinst, const char *name);

/* Set the supported extensions. The provided string is parsed into a list
   of extensions that are to be enabled/disabled. */
void sieve_set_extensions(struct sieve_instance *svinst,
			  const char *extensions);


/* Get top-level event for this Sieve instance. */
struct event *sieve_get_event(struct sieve_instance *svinst) ATTR_PURE;

/*
 * Script compilation
 */

/* Compile a Sieve script from a Sieve script object. Returns Sieve binary upon
   success and NULL upon failure. */
struct sieve_binary *
sieve_compile_script(struct sieve_script *script,
		     struct sieve_error_handler *ehandler,
		     enum sieve_compile_flags flags, enum sieve_error *error_r)
		     ATTR_NULL(2, 4);

/* Compile a Sieve script from a Sieve script location string. Returns Sieve
   binary upon success and NULL upon failure. The provided script_name is used
   for the internally created Sieve script object. */
struct sieve_binary *
sieve_compile(struct sieve_instance *svinst, const char *script_location,
	      const char *script_name, struct sieve_error_handler *ehandler,
	      enum sieve_compile_flags flags, enum sieve_error *error_r)
	      ATTR_NULL(3, 4, 6);

/*
 * Reading/writing Sieve binaries
 */

/* Loads the sieve binary indicated by the provided path. */
struct sieve_binary *
sieve_load(struct sieve_instance *svinst, const char *bin_path,
	   enum sieve_error *error_r);
/* First tries to open the binary version of the specified script and if it does
   not exist or if it contains errors, the script is (re-)compiled. Note that
   errors in the bytecode are caught only at runtime.
 */
struct sieve_binary *
sieve_open_script(struct sieve_script *script,
		  struct sieve_error_handler *ehandler,
		  enum sieve_compile_flags flags, enum sieve_error *error_r);
/* First tries to open the binary version of the specified script and if it does
   not exist or if it contains errors, the script is (re-)compiled. Note that
   errors in the bytecode are caught only at runtime.
 */
struct sieve_binary *
sieve_open(struct sieve_instance *svinst, const char *script_location,
	   const char *script_name, struct sieve_error_handler *ehandler,
	   enum sieve_compile_flags flags, enum sieve_error *error_r);

/* Saves the binary as the file indicated by the path parameter. This function
   will not write the binary to disk when the provided binary object was loaded
   earlier from the indicated bin_path, unless update is TRUE.
 */
int sieve_save_as(struct sieve_binary *sbin, const char *bin_path, bool update,
		  mode_t save_mode, enum sieve_error *error_r);

/* Saves the binary to the default location. This function will not overwrite
   the binary on disk when the provided binary object was loaded earlier from
   the default location, unless update is TRUE.
 */
int sieve_save(struct sieve_binary *sbin, bool update,
	       enum sieve_error *error_r);

/* Closes a compiled/opened sieve binary. */
void sieve_close(struct sieve_binary **_sbin);

/* Obtains the path the binary was compiled or loaded from. */
const char *sieve_get_source(struct sieve_binary *sbin);
/* Indicates whether the binary was loaded from a pre-compiled file. */
bool sieve_is_loaded(struct sieve_binary *sbin);

/*
 * Debugging
 */

/* Dumps the byte code in human-readable form to the specified ostream. */
void sieve_dump(struct sieve_binary *sbin,
		struct ostream *stream, bool verbose);
/* Dumps the byte code in hexdump form to the specified ostream. */
void sieve_hexdump(struct sieve_binary *sbin, struct ostream *stream);

/* Executes the bytecode, but only prints the result to the given stream. */
int sieve_test(struct sieve_binary *sbin,
	       const struct sieve_message_data *msgdata,
	       const struct sieve_script_env *senv,
	       struct sieve_error_handler *ehandler, struct ostream *stream,
	       enum sieve_execute_flags flags, bool *keep);

/*
 * Script execution
 */

/* Initializes the scirpt environment from the given mail_user. */
int sieve_script_env_init(struct sieve_script_env *senv, struct mail_user *user,
			  const char **error_r);

/* Executes the binary, including the result. */
int sieve_execute(struct sieve_binary *sbin,
		  const struct sieve_message_data *msgdata,
		  const struct sieve_script_env *senv,
		  struct sieve_error_handler *exec_ehandler,
		  struct sieve_error_handler *action_ehandler,
		  enum sieve_execute_flags flags, bool *keep);

/*
 * Multiscript support
 */

struct sieve_multiscript;

struct sieve_multiscript *
sieve_multiscript_start_execute(struct sieve_instance *svinst,
				const struct sieve_message_data *msgdata,
				const struct sieve_script_env *senv);
struct sieve_multiscript *
sieve_multiscript_start_test(struct sieve_instance *svinst,
			     const struct sieve_message_data *msgdata,
			     const struct sieve_script_env *senv,
			     struct ostream *stream);

bool sieve_multiscript_run(struct sieve_multiscript *mscript,
			   struct sieve_binary *sbin,
			   struct sieve_error_handler *exec_ehandler,
			   struct sieve_error_handler *action_ehandler,
			   enum sieve_execute_flags flags);

bool sieve_multiscript_will_discard(struct sieve_multiscript *mscript);
void sieve_multiscript_run_discard(struct sieve_multiscript *mscript,
				   struct sieve_binary *sbin,
				   struct sieve_error_handler *exec_ehandler,
				   struct sieve_error_handler *action_ehandler,
				   enum sieve_execute_flags flags);

int sieve_multiscript_status(struct sieve_multiscript *mscript);

int sieve_multiscript_tempfail(struct sieve_multiscript **_mscript,
			       struct sieve_error_handler *action_ehandler,
			       enum sieve_execute_flags flags);
int sieve_multiscript_finish(struct sieve_multiscript **_mscript,
			     struct sieve_error_handler *action_ehandler,
			     enum sieve_execute_flags flags, bool *keep);

/*
 * Configured limits
 */

unsigned int sieve_max_redirects(struct sieve_instance *svinst);
unsigned int sieve_max_actions(struct sieve_instance *svinst);
size_t sieve_max_script_size(struct sieve_instance *svinst);

/*
 * User log
 */

const char *sieve_user_get_log_path(struct sieve_instance *svinst,
				    struct sieve_script *user_script)
				    ATTR_NULL(2);

/*
 * Script trace log
 */

struct sieve_trace_log;

int sieve_trace_log_create(struct sieve_instance *svinst, const char *path,
			   struct sieve_trace_log **trace_log_r) ATTR_NULL(2);
int sieve_trace_log_create_dir(struct sieve_instance *svinst, const char *dir,
			       struct sieve_trace_log **trace_log_r)
			       ATTR_NULL(3);

int sieve_trace_log_open(struct sieve_instance *svinst,
			 struct sieve_trace_log **trace_log_r) ATTR_NULL(2);

void sieve_trace_log_printf(struct sieve_trace_log *trace_log,
			    const char *fmt, ...) ATTR_FORMAT(2, 3);

void sieve_trace_log_free(struct sieve_trace_log **_trace_log);

int sieve_trace_config_get(struct sieve_instance *svinst,
			   struct sieve_trace_config *tr_config);

#endif
