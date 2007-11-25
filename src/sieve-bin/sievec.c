#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "lib.h"
#include "str.h"
#include "istream.h"
#include "buffer.h"

#include "sieve.h"

static int _open_fd(const char *path)
{
	return open(path, O_RDONLY);
}

int main(int argc, char **argv) {
	int fd;
	struct sieve_binary *sbin;
	
	if ( argc < 2 ) {
		printf( "Usage: sievec <filename>\n");
 		exit(1);
 	}
  
	if ( (fd = _open_fd(argv[1])) < 0 ) {
		perror("open()");
		exit(1);
	}

	printf("Parsing sieve script '%s'...\n", argv[1]);

	if ( sieve_init("") ) {
		sbin = sieve_compile(fd, TRUE);
	
		if ( sbin != NULL ) 
			(void) sieve_dump(sbin);

		sieve_deinit();
	} else {
		printf("Failed to initialize sieve implementation.");
	}

 	close(fd);
}
