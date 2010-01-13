/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "array.h"

#include "managesieve-common.h"
#include "managesieve-commands.h"

#include <stdlib.h>

/* Might want to combine this somewhere in a commands-common.c 
 * to avoid duplicate code 
 */

const struct command managesieve_commands[] = {
	{ "CAPABILITY", cmd_capability },
	{ "LOGOUT", cmd_logout },
	{ "PUTSCRIPT", cmd_putscript },
	{ "CHECKSCRIPT", cmd_checkscript },
	{ "GETSCRIPT", cmd_getscript },
	{ "SETACTIVE", cmd_setactive },
	{ "DELETESCRIPT", cmd_deletescript },
	{ "LISTSCRIPTS", cmd_listscripts },
	{ "HAVESPACE", cmd_havespace },
	{ "RENAMESCRIPT", cmd_renamescript },
	{ "NOOP", cmd_noop }
};

#define MANAGESIEVE_COMMANDS_COUNT N_ELEMENTS(managesieve_commands) 

static ARRAY_DEFINE(commands, struct command);
static bool commands_unsorted;

void command_register(const char *name, command_func_t *func)
{
	struct command cmd;

	cmd.name = name;
	cmd.func = func;
	array_append(&commands, &cmd, 1);

	commands_unsorted = TRUE;
}

void command_unregister(const char *name)
{
	const struct command *cmd;
	unsigned int i, count;

	cmd = array_get(&commands, &count);
	for (i = 0; i < count; i++) {
		if (strcasecmp(cmd[i].name, name) == 0) {
			array_delete(&commands, i, 1);
			return;
		}
	}

	i_error("Trying to unregister unknown command '%s'", name);
}

void command_register_array(const struct command *cmdarr, unsigned int count)
{
	commands_unsorted = TRUE;
	array_append(&commands, cmdarr, count);
}

void command_unregister_array(const struct command *cmdarr, unsigned int count)
{
	while (count > 0) {
		command_unregister(cmdarr->name);
		count--; cmdarr++;
	}
}

static int command_cmp(const void *p1, const void *p2)
{
	const struct command *c1 = p1, *c2 = p2;

	return strcasecmp(c1->name, c2->name);
}

static int command_bsearch(const void *name, const void *cmd_p)
{
	const struct command *cmd = cmd_p;

	return strcasecmp(name, cmd->name);
}

struct command *command_find(const char *name)
{
    void *base;
    unsigned int count;

    base = array_get_modifiable(&commands, &count);
    if (commands_unsorted) {
        qsort(base, count, sizeof(struct command), command_cmp);
                commands_unsorted = FALSE;
    }

    return bsearch(name, base, count, sizeof(struct command),
               command_bsearch);
}

void commands_init(void)
{
	i_array_init(&commands, 16);
	commands_unsorted = FALSE;
	
	command_register_array(managesieve_commands, MANAGESIEVE_COMMANDS_COUNT);
}

void commands_deinit(void)
{
	command_unregister_array(managesieve_commands, MANAGESIEVE_COMMANDS_COUNT);
	array_free(&commands);
}
