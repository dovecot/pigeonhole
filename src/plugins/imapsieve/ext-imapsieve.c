/* Copyright (c) 2016-2018 Pigeonhole authors, see the included COPYING file
 */

/* Extension imapsieve
 * -------------------
 *
 * Authors: Stephan Bosch
 * Specification: RFC 6785
 * Implementation: full
 * Status: experimental
 *
 */

#include "lib.h"

#include "sieve-extensions.h"
#include "sieve-commands.h"
#include "sieve-binary.h"

#include "sieve-validator.h"
#include "sieve-interpreter.h"

#include "sieve-ext-environment.h"

#include "ext-imapsieve-common.h"

/*
 * Extension
 */

static bool ext_imapsieve_load
	(const struct sieve_extension *ext, void **context);
static bool ext_vnd_imapsieve_load
	(const struct sieve_extension *ext, void **context);
static bool ext_vnd_imapsieve_validator_load
	(const struct sieve_extension *ext, struct sieve_validator *valdtr);

static bool ext_imapsieve_interpreter_load
	(const struct sieve_extension *ext,
		const struct sieve_runtime_env *renv,
		sieve_size_t *address ATTR_UNUSED);

#ifdef __IMAPSIEVE_DUMMY
const struct sieve_extension_def imapsieve_extension_dummy = {
#else
const struct sieve_extension_def imapsieve_extension = {
#endif
	.name = "imapsieve",
	.load = ext_imapsieve_load,
	.interpreter_load = ext_imapsieve_interpreter_load
};

#ifdef __IMAPSIEVE_DUMMY
const struct sieve_extension_def vnd_imapsieve_extension_dummy = {
#else
const struct sieve_extension_def vnd_imapsieve_extension = {
#endif
	.name = "vnd.dovecot.imapsieve",
	.load = ext_vnd_imapsieve_load,
	.interpreter_load = ext_imapsieve_interpreter_load,
	.validator_load = ext_vnd_imapsieve_validator_load
};

/*
 * Context
 */

static bool ext_imapsieve_load
(const struct sieve_extension *ext, void **context)
{
	*context = (void*)
		sieve_ext_environment_require_extension(ext->svinst);
	return TRUE;
}

static bool ext_vnd_imapsieve_load
(const struct sieve_extension *ext, void **context)
{
	*context = (void*)sieve_extension_require
#ifdef __IMAPSIEVE_DUMMY
		(ext->svinst, &imapsieve_extension_dummy, TRUE);
#else
		(ext->svinst, &imapsieve_extension, TRUE);
#endif
	return TRUE;
}

/*
 * Validator
 */

static bool ext_vnd_imapsieve_validator_load
(const struct sieve_extension *ext ATTR_UNUSED,
	struct sieve_validator *valdtr)
{
	const struct sieve_extension *ims_ext;

	/* Load environment extension implicitly */

	ims_ext = sieve_validator_extension_load_implicit
#ifdef __IMAPSIEVE_DUMMY
		(valdtr, imapsieve_extension_dummy.name);
#else
		(valdtr, imapsieve_extension.name);
#endif
	if ( ims_ext == NULL )
		return FALSE;

	return TRUE;
}

/*
 * Interpreter
 */

static int ext_imapsieve_interpreter_run
	(const struct sieve_extension *this_ext,
		const struct sieve_runtime_env *renv,
		void *context, bool deferred);

const struct sieve_interpreter_extension
imapsieve_interpreter_extension = {
#ifdef __IMAPSIEVE_DUMMY
	.ext_def = &imapsieve_extension_dummy,
#else
	.ext_def = &imapsieve_extension,
#endif
	.run = ext_imapsieve_interpreter_run
};

static bool ext_imapsieve_interpreter_load
(const struct sieve_extension *ext ATTR_UNUSED,
	const struct sieve_runtime_env *renv,
	sieve_size_t *address ATTR_UNUSED)
{
	sieve_interpreter_extension_register(renv->interp,
		ext, &imapsieve_interpreter_extension, NULL);
	return TRUE;
}

#ifdef __IMAPSIEVE_DUMMY
static int ext_imapsieve_interpreter_run
(const struct sieve_extension *ext ATTR_UNUSED,
	const struct sieve_runtime_env *renv,
	void *context ATTR_UNUSED, bool deferred)
{
	if ( !deferred ) {
		sieve_runtime_error(renv, NULL,
			"the imapsieve extension cannot be used outside IMAP");
	}
	return SIEVE_EXEC_FAILURE;
}
#else
static int ext_imapsieve_interpreter_run
(const struct sieve_extension *ext,
	const struct sieve_runtime_env *renv,
	void *context ATTR_UNUSED, bool deferred ATTR_UNUSED)
{
	if (ext->def == &vnd_imapsieve_extension) {
		const struct sieve_extension *ims_ext =
			(const struct sieve_extension *)ext->context;
		ext_imapsieve_environment_vendor_items_register(ims_ext, renv);
	} else {
		ext_imapsieve_environment_items_register(ext, renv);
	}
	return SIEVE_EXEC_OK;
}
#endif
