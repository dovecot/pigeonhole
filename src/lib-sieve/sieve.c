/* Copyright (c) 2002-2007 Dovecot authors, see the included COPYING file */

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
#include "sieve-code-dumper.h"

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

static struct sieve_ast *sieve_parse
	(int fd, const char *scriptname, struct sieve_error_handler *ehandler)
{
	struct sieve_parser *parser;
	struct sieve_ast *ast;
	
	/* Parse */
	parser = sieve_parser_create(fd, scriptname, ehandler);

 	if ( !sieve_parser_run(parser, &ast) || sieve_get_errors(ehandler) > 0 ) {
 		if ( ast != NULL )
 			/* This really shouldn't happen */
 			sieve_ast_unref(&ast);
 		ast = NULL;
 	} else 
		sieve_ast_ref(ast);
	
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

static struct sieve_binary *sieve_compile_fd
(int fd, const char *scriptname, struct sieve_error_handler *ehandler) 
{
	struct sieve_binary *result;
	struct sieve_ast *ast;
  	
	/* Parse */
	if ( (ast = sieve_parse(fd, scriptname, ehandler)) == NULL ) {
 		sieve_error(ehandler, scriptname, "failed to parse script");
		return NULL;
	}

	/* Validate */
	if ( !sieve_validate(ast, ehandler) ) {
		sieve_error(ehandler, scriptname, "failed to validate script");
		
 		sieve_ast_unref(&ast);
 		return NULL;
 	}
 	
	/* Generate */
	if ( (result=sieve_generate(ast)) == NULL ) {
		sieve_error(ehandler, scriptname, "failed to generate script");
		
		sieve_ast_unref(&ast);
		return NULL;
	}
	
	/* Cleanup */
	sieve_ast_unref(&ast);

	return result;
}

struct sieve_binary *sieve_compile
	(const char *scriptpath, struct sieve_error_handler *ehandler)
{
	int sfd;
	const char *scriptname;
	struct sieve_binary *sbin;
	
	
	if ( (sfd = open(scriptpath, O_RDONLY)) < 0 ) {
		sieve_error(ehandler, scriptpath, "failed to open sieve script: %m");
		return NULL;
	}
	
	scriptname = strrchr(scriptpath, '/');
	if ( scriptname == NULL )	
		scriptname = scriptpath;
	else
		scriptname++;
	
	sbin = sieve_compile_fd(sfd, scriptname, ehandler);
		
	close(sfd);
	return sbin;
}

void sieve_dump(struct sieve_binary *binary, struct ostream *stream) 
{
	struct sieve_code_dumper *dumpr = sieve_code_dumper_create(binary);			

	sieve_code_dumper_run(dumpr, stream);	
	
	sieve_code_dumper_free(dumpr);
}

bool sieve_test
	(struct sieve_binary *binary, const struct sieve_message_data *msgdata,
		const struct sieve_mail_environment *menv) 	
{
	struct sieve_result *sres = sieve_result_create();
	struct sieve_interpreter *interp = sieve_interpreter_create(binary);			
	bool result = TRUE;
							
	result = sieve_interpreter_run(interp, msgdata, menv, &sres);
	
	if ( result ) 
		sieve_result_print(sres);
	
	sieve_interpreter_free(interp);
	sieve_result_unref(&sres);
	return result;
}

bool sieve_execute
	(struct sieve_binary *binary, const struct sieve_message_data *msgdata,
		const struct sieve_mail_environment *menv) 	
{
	struct sieve_result *sres = NULL;
	struct sieve_interpreter *interp = sieve_interpreter_create(binary);			
	bool result = TRUE;
							
	result = sieve_interpreter_run(interp, msgdata, menv, &sres);
				
	sieve_interpreter_free(interp);
	sieve_result_unref(&sres);
	return result;
}


	
