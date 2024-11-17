#ifndef SIEVE_INTERPRETER_H
#define SIEVE_INTERPRETER_H

#include "lib.h"
#include "array.h"
#include "buffer.h"

#include "sieve-common.h"
#include "sieve-runtime.h"

/*
 * Interpreter
 */

struct sieve_interpreter *
sieve_interpreter_create(struct sieve_binary *sbin,
			 struct sieve_interpreter *parent,
			 const struct sieve_execute_env *eenv,
			 struct sieve_error_handler *ehandler) ATTR_NULL(2);
struct sieve_interpreter *
sieve_interpreter_create_for_block(struct sieve_binary_block *sblock,
				   struct sieve_script *script,
				   struct sieve_interpreter *parent,
				   const struct sieve_execute_env *eenv,
				   struct sieve_error_handler *ehandler)
				   ATTR_NULL(3);
void sieve_interpreter_free(struct sieve_interpreter **_interp);

/*
 * Accessors
 */

pool_t sieve_interpreter_pool(struct sieve_interpreter *interp);
struct sieve_interpreter *
sieve_interpreter_get_parent(struct sieve_interpreter *interp);
struct sieve_script *sieve_interpreter_script(struct sieve_interpreter *interp);
struct sieve_error_handler *
sieve_interpreter_get_error_handler(struct sieve_interpreter *interp);
struct sieve_instance *
sieve_interpreter_svinst(struct sieve_interpreter *interp);

/* Do not use this function for normal sieve extensions. This is intended for
 * the testsuite only.
 */
void sieve_interpreter_set_result(struct sieve_interpreter *interp,
				  struct sieve_result *result);

/*
 * Loop handling
 */

struct sieve_interpreter_loop;

int sieve_interpreter_loop_start(struct sieve_interpreter *interp,
				 sieve_size_t loop_end,
				 const struct sieve_extension_def *ext_def,
				 struct sieve_interpreter_loop **loop_r);
struct sieve_interpreter_loop *
sieve_interpreter_loop_get(struct sieve_interpreter *interp,
			   sieve_size_t loop_end,
			   const struct sieve_extension_def *ext_def);
int sieve_interpreter_loop_next(struct sieve_interpreter *interp,
				struct sieve_interpreter_loop *loop,
				sieve_size_t loop_begin);
int sieve_interpreter_loop_break(struct sieve_interpreter *interp,
				 struct sieve_interpreter_loop *loop);

struct sieve_interpreter_loop *
sieve_interpreter_loop_get_local(struct sieve_interpreter *interp,
				 struct sieve_interpreter_loop *loop,
				 const struct sieve_extension_def *ext_def)
				 ATTR_NULL(2, 3);
struct sieve_interpreter_loop *
sieve_interpreter_loop_get_global(struct sieve_interpreter *interp,
				  struct sieve_interpreter_loop *loop,
				  const struct sieve_extension_def *ext_def)
				  ATTR_NULL(2, 3);

pool_t sieve_interpreter_loop_get_pool(struct sieve_interpreter_loop *loop)
				       ATTR_PURE;
void *sieve_interpreter_loop_get_context(struct sieve_interpreter_loop *loop)
					 ATTR_PURE;
void sieve_interpreter_loop_set_context(struct sieve_interpreter_loop *loop,
					void *context);

/*
 * Program flow
 */

void sieve_interpreter_reset(struct sieve_interpreter *interp);
void sieve_interpreter_interrupt(struct sieve_interpreter *interp);
sieve_size_t
sieve_interpreter_program_counter(struct sieve_interpreter *interp);

int sieve_interpreter_program_jump_to(struct sieve_interpreter *interp,
				      sieve_size_t jmp_target,
				      bool break_loops);
int sieve_interpreter_program_jump(struct sieve_interpreter *interp, bool jump,
				   bool break_loops);

/*
 * Test results
 */

void sieve_interpreter_set_test_result(struct sieve_interpreter *interp,
				       bool result);
bool sieve_interpreter_get_test_result(struct sieve_interpreter *interp);

/*
 * Source location
 */

unsigned int
sieve_runtime_get_source_location(const struct sieve_runtime_env *renv,
				  sieve_size_t code_address);

unsigned int
sieve_runtime_get_command_location(const struct sieve_runtime_env *renv);
const char *
sieve_runtime_get_full_command_location(const struct sieve_runtime_env *renv);

/*
 * Extension support
 */

struct sieve_interpreter_extension {
	const struct sieve_extension_def *ext_def;

	int (*run)(const struct sieve_extension *ext,
		   const struct sieve_runtime_env *renv,
		   void *context, bool deferred);
	void (*free)(const struct sieve_extension *ext,
		     struct sieve_interpreter *interp, void *context);
};

void sieve_interpreter_extension_register(
	struct sieve_interpreter *interp, const struct sieve_extension *ext,
	const struct sieve_interpreter_extension *intext, void *context);
void sieve_interpreter_extension_set_context(
	struct sieve_interpreter *interp, const struct sieve_extension *ext,
	void *context);
void *sieve_interpreter_extension_get_context(
	struct sieve_interpreter *interp, const struct sieve_extension *ext);

int sieve_interpreter_extension_start(struct sieve_interpreter *interp,
				      const struct sieve_extension *ext);

/*
 * Opcodes and operands
 */

int sieve_interpreter_handle_optional_operands(
	const struct sieve_runtime_env *renv, sieve_size_t *address,
	struct sieve_side_effects_list **list);

/*
 * Code execute
 */

int sieve_interpreter_continue(struct sieve_interpreter *interp,
			       bool *interrupted);
int sieve_interpreter_start(struct sieve_interpreter *interp,
			    struct sieve_result *result, bool *interrupted);
int sieve_interpreter_run(struct sieve_interpreter *interp,
			  struct sieve_result *result);

/*
 * Error handling
 */

void sieve_runtime_error(const struct sieve_runtime_env *renv,
			 const char *csrc_filename, unsigned int csrc_linenum,
			 const char *location, const char *fmt, ...)
			 ATTR_FORMAT(5, 6);
#define sieve_runtime_error(renv, ...) \
	sieve_runtime_error(renv, __FILE__, __LINE__, __VA_ARGS__)
void sieve_runtime_warning(const struct sieve_runtime_env *renv,
			   const char *csrc_filename, unsigned int csrc_linenum,
			   const char *location, const char *fmt, ...)
			   ATTR_FORMAT(5, 6);
#define sieve_runtime_warning(renv, ...) \
	sieve_runtime_warning(renv, __FILE__, __LINE__, __VA_ARGS__)
void sieve_runtime_log(const struct sieve_runtime_env *renv,
		       const char *csrc_filename, unsigned int csrc_linenum,
		       const char *location, const char *fmt, ...)
		       ATTR_FORMAT(5, 6);
#define sieve_runtime_log(renv, ...) \
	sieve_runtime_log(renv, __FILE__, __LINE__, __VA_ARGS__)
void sieve_runtime_debug(const struct sieve_runtime_env *renv,
			 const char *csrc_filename, unsigned int csrc_linenum,
			 const char *location, const char *fmt, ...)
			 ATTR_FORMAT(5, 6);
#define sieve_runtime_debug(renv, ...) \
	sieve_runtime_debug(renv, __FILE__, __LINE__, __VA_ARGS__)
void sieve_runtime_critical(const struct sieve_runtime_env *renv,
			    const char *csrc_filename,
			    unsigned int csrc_linenum,
			    const char *location, const char *user_prefix,
			    const char *fmt, ...) ATTR_FORMAT(6, 7);
#define sieve_runtime_critical(renv, ...) \
	sieve_runtime_critical(renv, __FILE__, __LINE__, __VA_ARGS__)
int sieve_runtime_mail_error(const struct sieve_runtime_env *renv,
			     struct mail *mail, 
			     const char *csrc_filename,
			     unsigned int csrc_linenum,
			     const char *fmt, ...) ATTR_FORMAT(5, 6);
#define sieve_runtime_mail_error(renv, mail, ...) \
	sieve_runtime_mail_error(renv, mail, __FILE__, __LINE__, __VA_ARGS__)

#endif
