/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#ifndef __SIEVE_EXTPROGRAMS_COMMON_H
#define __SIEVE_EXTPROGRAMS_COMMON_H

#include "sieve-common.h"

/*
 * Extension configuration
 */

struct sieve_extprograms_config {
	const struct sieve_extension *copy_ext;
	const struct sieve_extension *var_ext;

	char *socket_dir;
	char *bin_dir;

	unsigned int execute_timeout;
};

struct sieve_extprograms_config *sieve_extprograms_config_init
	(const struct sieve_extension *ext);
void sieve_extprograms_config_deinit
	(struct sieve_extprograms_config **ext_config);

/*
 * Extensions
 */

extern const struct sieve_extension_def pipe_extension;
extern const struct sieve_extension_def filter_extension;
extern const struct sieve_extension_def execute_extension;

/* 
 * Commands 
 */

extern const struct sieve_command_def cmd_pipe;
extern const struct sieve_command_def cmd_filter;
extern const struct sieve_command_def cmd_execute;

/*
 * Operations
 */

extern const struct sieve_operation_def cmd_pipe_operation;
extern const struct sieve_operation_def cmd_filter_operation;
extern const struct sieve_operation_def cmd_execute_operation;

/*
 * Program name and arguments
 */

bool sieve_extprogram_arg_is_valid(string_t *arg);
bool sieve_extprogram_name_is_valid(string_t *name);

/*
 * Command validation
 */

bool sieve_extprogram_command_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);

/*
 * Common command operands
 */

int sieve_extprogram_command_read_operands
	(const struct sieve_runtime_env *renv, sieve_size_t *address,
		string_t **pname_r, struct sieve_stringlist **args_list_r);

/*
 * Running external programs
 */

void sieve_extprogram_exec_error
	(struct sieve_error_handler *ehandler, const char *location,
		const char *fmt, ...) ATTR_FORMAT(3, 4);

struct sieve_extprogram *sieve_extprogram_create
	(const struct sieve_extension *ext, const struct sieve_script_env *senv,
		const struct sieve_message_data *msgdata, const char *action,
		const char *program_name, const char * const *args,
		enum sieve_error *error_r);
void sieve_extprogram_destroy(struct sieve_extprogram **_sprog);

void sieve_extprogram_set_output
	(struct sieve_extprogram *sprog, struct ostream *output);
void sieve_extprogram_set_input
	(struct sieve_extprogram *sprog, struct istream *input);
int sieve_extprogram_set_input_mail
	(struct sieve_extprogram *sprog, struct mail *mail);

int sieve_extprogram_run(struct sieve_extprogram *sprog);

#endif /* __SIEVE_EXTPROGRAMS_COMMON_H */
