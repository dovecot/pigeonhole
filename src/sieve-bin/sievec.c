#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "lib.h"
#include "sieve.h"

#include "bin-common.h"

int main(int argc, char **argv) {
	struct sieve_binary *sbin;
	
	bin_init();
	
	if ( argc < 2 ) {
		printf( "Usage: sievec <filename>\n");
 		exit(1);
 	}
  
	sbin = bin_compile_sieve_script(argv[1]);
	bin_dump_sieve_binary_to(sbin, "-");

	bin_deinit();
}
