/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "str.h"

#include "sieve.h"
#include "sieve-storage.h"
#include "sieve-storage-script.h"

#include "managesieve-common.h"
#include "managesieve-commands.h"

bool cmd_setactive(struct client_command_context *cmd)
{
	struct client *client = cmd->client;
	struct sieve_storage *storage = client->storage;
	const char *scriptname;
	struct sieve_script *script;
	int ret;

	/* <scriptname> */
	if ( !client_read_string_args(cmd, 1, TRUE, &scriptname) )
		return FALSE;

	/* Activate, or .. */
	if ( *scriptname != '\0' ) {
		string_t *errors = NULL;
		bool warnings = FALSE;
		bool success = TRUE;

		script = sieve_storage_script_init(storage, scriptname);
		if ( script == NULL ) {
			client_send_storage_error(client, storage);
			return TRUE;
		}

		if ( sieve_storage_script_is_active(script) <= 0 ) {
			/* Script is first being activated; compile it again without the UPLOAD
			 * flag.
			 */
			T_BEGIN {
				struct sieve_error_handler *ehandler;
				enum sieve_compile_flags cpflags =
					SIEVE_COMPILE_FLAG_NOGLOBAL | SIEVE_COMPILE_FLAG_ACTIVATED;
				struct sieve_binary *sbin;

				/* Prepare error handler */
				errors = str_new(default_pool, 1024);
				ehandler = sieve_strbuf_ehandler_create(client->svinst, errors, TRUE,
					client->set->managesieve_max_compile_errors);

				/* Compile */
				if ( (sbin=sieve_compile_script
					(script, ehandler, cpflags, NULL)) == NULL ) {
					success = FALSE;
				} else {
					sieve_close(&sbin);
				}

				warnings = ( sieve_get_warnings(ehandler) > 0 );
				sieve_error_handler_unref(&ehandler);
			} T_END;
		}

		/* Activate only when script is valid (or already active) */
		if ( success ) {
			/* Refresh activation no matter what; this can also resolve some erroneous
			 * situations.
			 */
			ret = sieve_storage_script_activate(script, (time_t)-1);
			if ( ret < 0 ) {
				client_send_storage_error(client, storage);
			} else {
				if ( warnings ) {
					client_send_okresp(client, "WARNINGS", str_c(errors));
				} else {
					client_send_ok(client, ( ret > 0 ?
						"Setactive completed." :
						"Script is already active." ));
				}
			}
		} else {
			client_send_no(client, str_c(errors));
		}

		if ( errors != NULL )
			str_free(&errors);
		sieve_script_unref(&script);

	/* ... deactivate */
	} else {
		ret = sieve_storage_deactivate(storage, (time_t)-1);

		if ( ret < 0 )
			client_send_storage_error(client, storage);
		else
			client_send_ok(client, ( ret > 0 ?
 				"Active script is now deactivated." :
				"No scripts currently active." ));
	}

	return TRUE;
}
