#include "lib.h"
#include "str.h"
#include "istream.h"
#include "buffer.h"

#include "sieve-extensions.h"

#include "sieve-ast.h"
#include "sieve-binary.h"
#include "sieve-result.h"

#include "sieve-parser.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"

#include "sieve.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

bool sieve_init(const char *plugins)
{
	return sieve_extensions_init(plugins);
}

void sieve_deinit(void)
{
	sieve_extensions_deinit();
}

static struct sieve_ast *sieve_parse(int fd, struct sieve_error_handler *ehandler)
{
	struct sieve_parser *parser;
	struct sieve_ast *ast;
	
	/* Construct ast */
	ast = sieve_ast_create();

	/* Parse */
	parser = sieve_parser_create(fd, ast, ehandler);

 	if ( !sieve_parser_run(parser) || sieve_get_errors(ehandler) > 0 ) {
 		/* Failed */
 		sieve_ast_unref(&ast); 
 		ast = NULL; /* Explicit */
 	} else 
	
	sieve_parser_free(parser); 	
	
	return ast;
}

static bool sieve_validate(struct sieve_ast *ast, struct sieve_error_handler *ehandler)
{
	bool result = TRUE;
	struct sieve_validator *validator = sieve_validator_create(ast, ehandler);
		
	if ( !sieve_validator_run(validator) || sieve_get_errors(ehandler) > 0 ) 
		result = FALSE;
	
	sieve_validator_free(validator);	
		
	return result;
}

static struct sieve_binary *sieve_generate(struct sieve_ast *ast)
{
	struct sieve_generator *generator = sieve_generator_create(ast);
	struct sieve_binary *result;
		
	result = sieve_generator_run(generator);
	
	sieve_generator_free(generator);
	
	return result;
}

struct sieve_binary *sieve_compile(int fd) 
{
	struct sieve_binary *result;
	struct sieve_error_handler *ehandler;
	struct sieve_ast *ast;
  
	/* Construct error handler */
	ehandler = sieve_error_handler_create();  
	
	/* Parse */

	printf("Parsing sieve script...\n");
	
	if ( (ast = sieve_parse(fd, ehandler)) == NULL ) {
 		printf("Parse failed.\n");
 		return NULL;
 	}

 	printf("Parse successful.\n");
 	sieve_ast_unparse(ast);

	/* Validate */
	
	printf("Validating script...\n");
	
	if ( !sieve_validate(ast, ehandler) ) {
		printf("Validation failed.\n");
		
 		sieve_ast_unref(&ast);
 		return NULL;
 	}
 	
 	printf("Validation successful.\n");

	/* Generate */
	
	printf("Generating script...\n");
	
	if ( (result=sieve_generate(ast)) == NULL ) {
		printf("Script generation failed.\n");
		
		sieve_ast_unref(&ast);
		return NULL;
	}
		
	printf("Script generation successful.\n");
	
	/* Cleanup */
	sieve_ast_unref(&ast);

	return result;
}

void sieve_dump(struct sieve_binary *binary) 
{
	struct sieve_interpreter *interpreter = sieve_interpreter_create(binary);			

	printf("Code Dump:\n\n");
	sieve_interpreter_dump_code(interpreter);	
	
	sieve_interpreter_free(interpreter);
}
	
bool sieve_execute
	(struct sieve_binary *binary, struct sieve_message_data *msgdata) 
{
	struct sieve_result *sres = sieve_result_create();
	struct sieve_interpreter *interpreter = sieve_interpreter_create(binary);			
	bool result = TRUE;
							
	printf("Code Execute:\n\n");
	if ( !sieve_interpreter_run(interpreter, msgdata, sres) ) {
		result = FALSE;
	}
				
	sieve_interpreter_free(interpreter);
	
	sieve_result_unref(&sres);
	return result;
}
	
