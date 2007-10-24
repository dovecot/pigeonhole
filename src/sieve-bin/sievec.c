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

#include "sieve-parser.h"
#include "sieve-ast.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

static int _open_fd(const char *path)
{
	return open(path, O_RDONLY);
}

int main(int argc, char **argv) {
	int fd;
	struct sieve_error_handler *ehandler;
	
	struct sieve_ast *ast;
	struct sieve_parser *parser;
	struct sieve_validator *validator;
	struct sieve_generator *generator;
	struct sieve_interpreter *interpreter;
	struct sieve_binary *binary;
	
	if ( argc < 2 ) {
		printf( "Usage: sievec <filename>\n");
 		exit(1);
 	}
  
	if ( (fd = _open_fd(argv[1])) < 0 ) {
		perror("open()");
		exit(1);
	}

	printf("Parsing sieve script '%s'...\n", argv[1]);

	/* Construct ast */
	ast = sieve_ast_create();
  
	/* Construct error handler */
	ehandler = sieve_error_handler_create();  
	
	/* Construct parser */
	parser = sieve_parser_create(fd, ast, ehandler);
  
 	if ( !sieve_parse(parser) || sieve_get_errors(ehandler) > 0 ) 
 		printf("Parse failed.\n");
 	else {
 		printf("Parse successful.\n");
 
 		sieve_ast_unparse(ast);
	
		printf("Validating script...\n");
		validator = sieve_validator_create(ast, ehandler);
		
		if ( !sieve_validate(validator) || sieve_get_errors(ehandler) > 0 ) {
			printf("Validation failed.\n");
		} else {
			printf("Validation successful.\n");
	
			printf("Generating script...\n"); 		
			generator = sieve_generator_create(ast);
	
			binary = sieve_generate(generator);
			if ( sieve_get_errors(ehandler) > 0 || (binary == NULL) ) {
				printf("Script generation failed.\n");
			} else {
				printf("Script generation successful.\n");
				
				printf("Code Dump:\n\n");
				interpreter = sieve_interpreter_create(binary);
				
				sieve_interpreter_dump_code(interpreter);
				
				printf("Code Execute:\n\n");
				(void) sieve_interpreter_run(interpreter);
				
				sieve_interpreter_free(interpreter);
			}
			
			sieve_generator_free(generator);
		}
		
		sieve_validator_free(validator);	
	}
 	
	sieve_parser_free(parser); 
	sieve_ast_unref(&ast);
  close(fd);
}
