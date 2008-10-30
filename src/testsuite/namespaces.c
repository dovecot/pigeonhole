/* Copyright (c) 2002-2008 Dovecot Sieve authors, see the included COPYING file
 */

#include "lib.h"
#include "istream.h"
#include "istream-seekable.h"
#include "fd-set-nonblock.h"
#include "str.h"
#include "str-sanitize.h"
#include "strescape.h"
#include "message-address.h"
#include "mail-storage.h"

#include "namespaces.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

#define DEFAULT_ENVELOPE_SENDER "MAILER-DAEMON"

void namespaces_init(void) 
{
	mail_storage_init();
	mail_storage_register_all();
	mailbox_list_register_all();
}	
	
void namespaces_deinit(void)
{
	mail_storage_deinit();
}

