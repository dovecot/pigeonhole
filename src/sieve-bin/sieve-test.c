/* Copyright (c) 2005-2007 Dovecot authors, see the included COPYING file */

#include "lib.h"

#include "bin-common.h"
#include "mail-raw.h"
#include "sieve.h"

#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_SENDMAIL_PATH "/usr/lib/sendmail"
#define DEFAULT_ENVELOPE_SENDER "MAILER-DAEMON"

int main(int argc, char **argv) 
{
	int fd;
	struct mail_raw *mailr;
	struct sieve_binary *sbin;
	struct sieve_message_data msgdata;

	bin_init();

	if ( argc < 2 ) {
		printf( "Usage: sieve_test <filename>\n");
 		exit(1);
 	}
  
  	/* Compile sieve script */
  
	if ( (fd = open(argv[1], O_RDONLY)) < 0 ) {
		perror("open()");
		exit(1);
	}

	printf("Parsing sieve script '%s'...\n", argv[1]);

	if ( !sieve_init("") ) {
		printf("Failed to initialize sieve implementation\n");
		exit(1);
	}

	if ( (sbin = sieve_compile(fd, TRUE)) == NULL ) {
		printf("Failed to compile sieve script\n");
		exit(1);
	}		 
		
	(void) sieve_dump(sbin);

 	close(fd);

	mail_raw_init();

	mailr = mail_raw_open(0);

	/* Collect necessary message data */
	memset(&msgdata, 0, sizeof(msgdata));
	msgdata.mail = mailr->mail;
	msgdata.return_path = "nico@example.com";
	msgdata.to_address = "sirius+sieve@rename-it.nl";
	msgdata.auth_user = "stephan";
	(void)mail_get_first_header(mailr->mail, "Message-ID", &msgdata.id);
	
	/* Run the test */
	(void) sieve_test(sbin, &msgdata);

	sieve_deinit();

	mail_raw_close(mailr);

	mail_raw_deinit();
	bin_deinit();  
	return 0;
}
