/* Copyright (c) 2002-2017 Pigeonhole authors, see the included COPYING file
 */

#ifndef __DOVEADM_SIEVE_CMD_H
#define __DOVEADM_SIEVE_CMD_H

struct doveadm_sieve_cmd_context;

struct doveadm_sieve_cmd_vfuncs {
	/* This is the main function which performs all the work for the
	   command. This is called once per each user. */
	int (*run)(struct doveadm_sieve_cmd_context *ctx);
};

struct doveadm_sieve_cmd_context {
	struct doveadm_mail_cmd_context ctx;

	struct sieve_instance *svinst;
	struct sieve_storage *storage;

	struct doveadm_sieve_cmd_vfuncs v;
};

void doveadm_sieve_cmd_failed_error
(struct doveadm_sieve_cmd_context *ctx, enum sieve_error error);
void doveadm_sieve_cmd_failed_storage
(struct doveadm_sieve_cmd_context *ctx,	 struct sieve_storage *storage);

#define doveadm_sieve_cmd_alloc(type) \
	(type *)doveadm_sieve_cmd_alloc_size(sizeof(type))
struct doveadm_sieve_cmd_context *
doveadm_sieve_cmd_alloc_size(size_t size);

void doveadm_sieve_cmd_scriptnames_check(const char *const args[]);

extern struct doveadm_cmd_ver2 doveadm_sieve_cmd_list;
extern struct doveadm_cmd_ver2 doveadm_sieve_cmd_get;
extern struct doveadm_cmd_ver2 doveadm_sieve_cmd_put;
extern struct doveadm_cmd_ver2 doveadm_sieve_cmd_delete;
extern struct doveadm_cmd_ver2 doveadm_sieve_cmd_activate;
extern struct doveadm_cmd_ver2 doveadm_sieve_cmd_deactivate;
extern struct doveadm_cmd_ver2 doveadm_sieve_cmd_rename;

void doveadm_sieve_cmds_init(void);

#endif
