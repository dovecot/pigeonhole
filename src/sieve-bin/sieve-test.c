/* Copyright (c) 2005-2007 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "ostream.h"

#include "bin-common.h"
#include "mail-raw.h"
#include "namespaces.h"
#include "sieve.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

#define DEFAULT_SENDMAIL_PATH "/usr/lib/sendmail"
#define DEFAULT_ENVELOPE_SENDER "MAILER-DAEMON"

int main(int argc, char **argv) 
{
	const char *user;
	int mfd;
	pool_t namespaces_pool;
	struct mail_raw *mailr;
	struct sieve_binary *sbin;
	struct sieve_message_data msgdata;
	struct sieve_mail_environment mailenv;

	bin_init();

	if ( argc < 2 ) {
		printf( "Usage: sieve-test <sieve-file> [<mailfile>/-]\n");
 		exit(1);
 	}

	/* Open mail file */
	if ( argc > 2 ) 
	{
		if ( strcmp(argv[2], "-") == 0 )
			mfd = 0;
		else {
			if ( (mfd = open(argv[2], O_RDONLY)) < 0 ) {
				perror("Failed to open mail file");
				exit(1);
			}
		}
	} else 
		mfd = 0;
	
	/* Compile sieve script */
	sbin = bin_compile_sieve_script(argv[1]);
	
	/* Dump script */
	bin_dump_sieve_binary_to(sbin, "-");
	
	user = bin_get_user();
	namespaces_pool = namespaces_init();
	mail_raw_init(namespaces_pool, user);

	mailr = mail_raw_open(mfd);

	/* Collect necessary message data */
	memset(&msgdata, 0, sizeof(msgdata));
	msgdata.mail = mailr->mail;
	msgdata.return_path = "nico@example.com";
	msgdata.to_address = "sirius+sieve@rename-it.nl";
	msgdata.auth_user = "stephan";
	(void)mail_get_first_header(mailr->mail, "Message-ID", &msgdata.id);

	memset(&mailenv, 0, sizeof(mailenv));
    mailenv.inbox = "INBOX";
	mailenv.username = "stephan";
	
	/* Run the test */
	(void) sieve_test(sbin, &msgdata, &mailenv);

	mail_raw_close(mailr);
	if ( mfd > 0 ) 
		close(mfd);

	mail_raw_deinit();
	namespaces_deinit();

	bin_deinit();  
	return 0;
}
