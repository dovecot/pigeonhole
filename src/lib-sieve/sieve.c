/* Copyright (c) 2002-2007 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "str.h"
#include "istream.h"
#include "buffer.h"

#include "sieve-extensions.h"

#include "sieve-script.h"
#include "sieve-ast.h"
#include "sieve-binary.h"
#include "sieve-result.h"

#include "sieve-parser.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code-dumper.h"

#include "sieve.h"
#include "sieve-common.h"

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

struct sieve_ast *sieve_parse
	(struct sieve_script *script, struct sieve_error_handler *ehandler)
{
	struct sieve_parser *parser;
	struct sieve_ast *ast;
	
	/* Parse */
	parser = sieve_parser_create(script, ehandler);

 	if ( !sieve_parser_run(parser, &ast) || sieve_get_errors(ehandler) > 0 ) {
 		if ( ast != NULL )
 			/* This really shouldn't happen */
 			sieve_ast_unref(&ast);
 		ast = NULL;
 	} else 
		sieve_ast_ref(ast);
	
	sieve_parser_free(&parser); 	
	
	return ast;
}

static bool sieve_validate(struct sieve_ast *ast, struct sieve_error_handler *ehandler)
{
	bool result = TRUE;
	struct sieve_validator *validator = sieve_validator_create(ast, ehandler);
		
	if ( !sieve_validator_run(validator) || sieve_get_errors(ehandler) > 0 ) 
		result = FALSE;
	
	sieve_validator_free(&validator);	
		
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

static struct sieve_binary *sieve_compile_script
(struct sieve_script *script, struct sieve_error_handler *ehandler) 
{
	struct sieve_ast *ast;
	struct sieve_binary *sbin;		
  	
	/* Parse */
	if ( (ast = sieve_parse(script, ehandler)) == NULL ) {
 		sieve_error(ehandler, sieve_script_name(script), "parse failed");
		return NULL;
	}

	/* Validate */
	if ( !sieve_validate(ast, ehandler) ) {
		sieve_error(ehandler, sieve_script_name(script), "validation failed");
		
 		sieve_ast_unref(&ast);
 		return NULL;
 	}
 	
	/* Generate */
	if ( (sbin=sieve_generate(ast)) == NULL ) {
		sieve_error(ehandler, sieve_script_name(script), "code generation failed");
		
		sieve_ast_unref(&ast);
		return NULL;
	}
	
	/* Cleanup */
	sieve_ast_unref(&ast);

	return sbin;
}

struct sieve_binary *sieve_compile
	(const char *script_path, struct sieve_error_handler *ehandler)
{
	struct sieve_script *script;
	struct sieve_binary *sbin;

	if ( (script = sieve_script_create(script_path, NULL, ehandler)) == NULL )
		return NULL;
	
	sbin = sieve_compile_script(script, ehandler);
	
	sieve_script_unref(&script);
	
	return sbin;
}

void sieve_dump(struct sieve_binary *sbin, struct ostream *stream) 
{
	struct sieve_code_dumper *dumpr = sieve_code_dumper_create(sbin);			

	sieve_code_dumper_run(dumpr, stream);	
	
	sieve_code_dumper_free(dumpr);
}

int sieve_test
	(struct sieve_binary *sbin, const struct sieve_message_data *msgdata,
		const struct sieve_script_env *senv, 
		struct sieve_error_handler *ehandler) 	
{
	struct sieve_result *sres = sieve_result_create(ehandler);
	struct sieve_interpreter *interp = 
		sieve_interpreter_create(sbin, ehandler);			
	int ret = 0;
							
	ret = sieve_interpreter_run(interp, msgdata, senv, &sres);
	
	if ( ret > 0 ) 
		ret = sieve_result_print(sres);
	
	sieve_interpreter_free(interp);
	sieve_result_unref(&sres);
	return ret;
}

int sieve_execute
	(struct sieve_binary *sbin, const struct sieve_message_data *msgdata,
		const struct sieve_script_env *senv,
		struct sieve_error_handler *ehandler) 	
{
	struct sieve_result *sres = NULL;
	struct sieve_interpreter *interp = 
		sieve_interpreter_create(sbin, ehandler);			
	int ret = 0;
							
	ret = sieve_interpreter_run(interp, msgdata, senv, &sres);
				
	sieve_interpreter_free(interp);
	sieve_result_unref(&sres);
	return ret;
}


	
