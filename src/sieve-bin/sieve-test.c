/* Copyright (c) 2005-2007 Dovecot authors, see the included COPYING file */

#include "lib.h"

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
    struct passwd *pw;
    uid_t process_euid;
	int sfd, mfd;
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
  
  	/* Open sieve script */
  
	if ( (sfd = open(argv[1], O_RDONLY)) < 0 ) {
		perror("Failed to open sieve script");
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

	printf("Parsing sieve script '%s'...\n", argv[1]);

	if ( !sieve_init("") ) {
		printf("Failed to initialize sieve implementation\n");
		exit(1);
	}

	if ( (sbin = sieve_compile(sfd, TRUE)) == NULL ) {
		printf("Failed to compile sieve script\n");
		exit(1);
	}		 
		
	(void) sieve_dump(sbin);

 	close(sfd);
	
    process_euid = geteuid();
    pw = getpwuid(process_euid);
    if (pw != NULL) {
        user = t_strdup(pw->pw_name);
    } else {
        i_fatal("Couldn't lookup our username (uid=%s)",
            dec2str(process_euid));
    }

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
	
	/* Run the test */
	(void) sieve_test(sbin, &msgdata, &mailenv);

	sieve_deinit();

	mail_raw_close(mailr);
	if ( mfd > 0 ) 
		close(mfd);

	mail_raw_deinit();
	namespaces_deinit();
	bin_deinit();  
	return 0;
}
