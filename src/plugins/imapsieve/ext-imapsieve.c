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

static int
ext_imapsieve_load(const struct sieve_extension *ext, void **context);
static int
ext_vnd_imapsieve_load(const struct sieve_extension *ext, void **context);
static void ext_imapsieve_unload(const struct sieve_extension *ext);
static void ext_vnd_imapsieve_unload(const struct sieve_extension *ext);

static bool
ext_vnd_imapsieve_validator_load(const struct sieve_extension *ext,
				 struct sieve_validator *valdtr);

static bool
ext_imapsieve_interpreter_load(const struct sieve_extension *ext,
			       const struct sieve_runtime_env *renv,
			       sieve_size_t *address ATTR_UNUSED);

#ifdef __IMAPSIEVE_DUMMY
const struct sieve_extension_def imapsieve_extension_dummy = {
#else
const struct sieve_extension_def imapsieve_extension = {
#endif
	.name = "imapsieve",
	.load = ext_imapsieve_load,
	.unload = ext_imapsieve_unload,
	.interpreter_load = ext_imapsieve_interpreter_load,
};

#ifdef __IMAPSIEVE_DUMMY
const struct sieve_extension_def vnd_imapsieve_extension_dummy = {
#else
const struct sieve_extension_def vnd_imapsieve_extension = {
#endif
	.name = "vnd.dovecot.imapsieve",
	.load = ext_vnd_imapsieve_load,
	.unload = ext_vnd_imapsieve_unload,
	.interpreter_load = ext_imapsieve_interpreter_load,
	.validator_load = ext_vnd_imapsieve_validator_load,
};

/*
 * Context
 */

static int
ext_imapsieve_load(const struct sieve_extension *ext, void **context)
{
	const struct sieve_extension *ext_environment;
	struct ext_imapsieve_context *extctx;

	if (context != NULL) {
		ext_imapsieve_unload(ext);
		*context = NULL;
	}

	if (sieve_ext_environment_require_extension(ext->svinst,
						    &ext_environment) < 0)
		return -1;

	extctx = i_new(struct ext_imapsieve_context, 1);
	extctx->ext_environment = ext_environment;

	*context = extctx;
	return 0;
}

static int
ext_vnd_imapsieve_load(const struct sieve_extension *ext, void **context)
{
	struct ext_vnd_imapsieve_context *extctx;

	if (context != NULL) {
		ext_imapsieve_unload(ext);
		*context = NULL;
	}

	extctx = i_new(struct ext_vnd_imapsieve_context, 1);
#ifdef __IMAPSIEVE_DUMMY
	if (sieve_extension_require(ext->svinst, &imapsieve_extension_dummy,
				    TRUE, &extctx->ext_imapsieve) < 0)
		return -1;
#else
	if (sieve_extension_require(ext->svinst, &imapsieve_extension,
				    TRUE, &extctx->ext_imapsieve) < 0)
		return -1;
#endif

	*context = extctx;
	return 0;
}

static void ext_imapsieve_unload(const struct sieve_extension *ext)
{
	struct ext_imapsieve_context *extctx = ext->context;

	i_free(extctx);
}

static void ext_vnd_imapsieve_unload(const struct sieve_extension *ext)
{
	struct ext_vnd_imapsieve_context *extctx = ext->context;

	i_free(extctx);
}

/*
 * Validator
 */

static bool
ext_vnd_imapsieve_validator_load(const struct sieve_extension *ext ATTR_UNUSED,
				 struct sieve_validator *valdtr)
{
	const struct sieve_extension *ims_ext;

	/* Load environment extension implicitly */

#ifdef __IMAPSIEVE_DUMMY
	ims_ext = sieve_validator_extension_load_implicit(
		valdtr, imapsieve_extension_dummy.name);
#else
	ims_ext = sieve_validator_extension_load_implicit(
		valdtr, imapsieve_extension.name);
#endif
	if (ims_ext == NULL)
		return FALSE;
	return TRUE;
}

/*
 * Interpreter
 */

static int
ext_imapsieve_interpreter_run(const struct sieve_extension *this_ext,
			      const struct sieve_runtime_env *renv,
			      void *context, bool deferred);

const struct sieve_interpreter_extension
imapsieve_interpreter_extension = {
#ifdef __IMAPSIEVE_DUMMY
	.ext_def = &imapsieve_extension_dummy,
#else
	.ext_def = &imapsieve_extension,
#endif
	.run = ext_imapsieve_interpreter_run,
};

static bool
ext_imapsieve_interpreter_load(const struct sieve_extension *ext ATTR_UNUSED,
			       const struct sieve_runtime_env *renv,
			       sieve_size_t *address ATTR_UNUSED)
{
	sieve_interpreter_extension_register(
		renv->interp, ext, &imapsieve_interpreter_extension, NULL);
	return TRUE;
}

#ifdef __IMAPSIEVE_DUMMY
static int
ext_imapsieve_interpreter_run(const struct sieve_extension *ext ATTR_UNUSED,
			      const struct sieve_runtime_env *renv,
			      void *context ATTR_UNUSED, bool deferred)
{
	if (!deferred) {
		sieve_runtime_error(renv, NULL,
			"the imapsieve extension cannot be used outside IMAP");
	}
	return SIEVE_EXEC_FAILURE;
}
#else
static int
ext_imapsieve_interpreter_run(const struct sieve_extension *ext,
			      const struct sieve_runtime_env *renv,
			      void *context ATTR_UNUSED,
			      bool deferred ATTR_UNUSED)
{
	if (ext->def == &vnd_imapsieve_extension) {
		struct ext_vnd_imapsieve_context *extctx = ext->context;
		const struct sieve_extension *ims_ext = extctx->ext_imapsieve;

		ext_imapsieve_environment_vendor_items_register(ims_ext, renv);
	} else {
		ext_imapsieve_environment_items_register(ext, renv);
	}
	return SIEVE_EXEC_OK;
}
#endif
