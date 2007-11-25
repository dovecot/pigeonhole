/* Copyright (c) 2005-2007 Dovecot authors, see the included COPYING file */

#include "lib.h"

#include "bin-common.h"
#include "mail-raw.h"
#include "sieve.h"

#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_SENDMAIL_PATH "/usr/lib/sendmail"
#define DEFAULT_ENVELOPE_SENDER "MAILER-DAEMON"

static int sieve_send_rejection
(const struct sieve_message_data *msgdata ATTR_UNUSED, 
	const char *recipient, const char *reason)
{
	i_info("<<NOT PERFORMED>> Rejected mail to %s with reason \"%s\"\n", 
		recipient, reason);  
	return 0;
}

static int sieve_send_forward
(const struct sieve_message_data *msgdata ATTR_UNUSED, 
	const char *forwardto)
{
	i_info("<<NOT PERFORMED>> Forwarded mail to %s.", forwardto);
	return 0;
}

int main(int argc, char **argv) 
{
	int sfd, mfd;
	struct mail_raw *mailr;
	struct sieve_binary *sbin;
	struct sieve_message_data msgdata;
	struct sieve_mail_environment mailenv;

	bin_init();

	if ( argc < 2 ) {
		printf( "Usage: sieve-exec <sieve-file> [<mailfile>/-]\n");
 		exit(1);
 	}
  
  	/* Open sieve script */
  
	if ( (sfd = open(argv[1], O_RDONLY)) < 0 ) {
		perror("open()");
		exit(1);
	}

 	/* Open mail file */
 
	if ( argc > 2 )
    {
        if ( *(argv[2]) == '-' && *(argv[2]+1) == '\0' )
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

	if ( (sbin = sieve_compile(sfd, FALSE)) == NULL ) {
		printf("Failed to compile sieve script\n");
		exit(1);
	}		 
		
	(void) sieve_dump(sbin);

 	close(sfd);

	mail_raw_init();

	mailr = mail_raw_open(mfd);

	/* Collect necessary message data */
	memset(&msgdata, 0, sizeof(msgdata));
	msgdata.mail = mailr->mail;
	msgdata.return_path = "nico@example.com";
	msgdata.to_address = "sirius+sieve@rename-it.nl";
	msgdata.auth_user = "stephan";
	(void)mail_get_first_header(mailr->mail, "Message-ID", &msgdata.id);
	
	memset(&mailenv, 0, sizeof(mailenv));
	mailenv.send_forward = sieve_send_forward;
	mailenv.send_rejection = sieve_send_rejection;
	
	/* Run */
	sieve_execute(sbin, &msgdata, &mailenv);

	sieve_deinit();

	mail_raw_close(mailr);
	close(mfd);

	mail_raw_deinit();
	bin_deinit();  
	return 0;
}
