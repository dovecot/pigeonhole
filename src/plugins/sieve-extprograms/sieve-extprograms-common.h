#ifndef SIEVE_EXTPROGRAMS_COMMON_H
#define SIEVE_EXTPROGRAMS_COMMON_H

#include "sieve-common.h"
#include "sieve-extprograms-settings.h"

/*
 * Extension configuration
 */

struct sieve_extprograms_ext_context {
	const struct sieve_extprograms_settings *set;

	const struct sieve_extension *copy_ext;
	const struct sieve_extension *var_ext;
};

int sieve_extprograms_ext_load(const struct sieve_extension *ext,
			       void **context_r);
void sieve_extprograms_ext_unload(const struct sieve_extension *ext);

/*
 * Extensions
 */

extern const struct sieve_extension_def sieve_ext_vnd_pipe;
extern const struct sieve_extension_def sieve_ext_vnd_filter;
extern const struct sieve_extension_def sieve_ext_vnd_execute;

/*
 * Commands
 */

extern const struct sieve_command_def sieve_cmd_pipe;
extern const struct sieve_command_def sieve_cmd_filter;
extern const struct sieve_command_def sieve_cmd_execute;

/*
 * Operations
 */

extern const struct sieve_operation_def sieve_opr_pipe;
extern const struct sieve_operation_def sieve_opr_filter;
extern const struct sieve_operation_def sieve_opr_execute;

/*
 * Program name and arguments
 */

bool sieve_extprogram_arg_is_valid(string_t *arg);
bool sieve_extprogram_name_is_valid(string_t *name);

/*
 * Command validation
 */

bool sieve_extprogram_command_validate(struct sieve_validator *valdtr,
				       struct sieve_command *cmd);

/*
 * Common command operands
 */

int sieve_extprogram_command_read_operands(
	const struct sieve_runtime_env *renv, sieve_size_t *address,
	string_t **pname_r, struct sieve_stringlist **args_list_r);

/*
 * Running external programs
 */

void sieve_extprogram_exec_error(struct sieve_error_handler *ehandler,
				 const char *location, const char *fmt, ...)
				 ATTR_FORMAT(3, 4);

struct sieve_extprogram *
sieve_extprogram_create(const struct sieve_extension *ext,
			const struct sieve_script_env *senv,
			const struct sieve_message_data *msgdata,
			const char *action, const char *program_name,
			const char *const *args,
			enum sieve_error *error_code_r);
void sieve_extprogram_destroy(struct sieve_extprogram **_sprog);

void sieve_extprogram_set_output(struct sieve_extprogram *sprog,
				 struct ostream *output);
void sieve_extprogram_set_output_seekable(struct sieve_extprogram *sprog);
struct istream *sieve_extprogram_get_output_seekable(
	struct sieve_extprogram *sprog);

void sieve_extprogram_set_input(struct sieve_extprogram *sprog,
				struct istream *input);
int sieve_extprogram_set_input_mail(struct sieve_extprogram *sprog,
				    struct mail *mail);

int sieve_extprogram_run(struct sieve_extprogram *sprog);

#endif
