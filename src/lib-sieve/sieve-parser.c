#include <stdio.h>

#include "lib.h"
#include "istream.h"
#include "failures.h"

#include "sieve-lexer.h"
#include "sieve-parser.h"
#include "sieve-error.h"
#include "sieve-ast.h"

/* FIXME: Enforce maximums on the number of arguments, tests, commands, nesting levels, etc.
 *
 */

#define SIEVE_READ_BLOCK_SIZE (1024*8)

#define sieve_parser_error(parser, ...) \
  { \
		/* Don't report a parse error if the lexer complained already */ \
		if ( sieve_lexer_current_token(parser->lexer) != STT_ERROR )  \
			sieve_error(parser->ehandler, sieve_lexer_current_line(parser->lexer), __VA_ARGS__); \
	}
#define sieve_parser_warning(parser, ...) \
	sieve_error(parser->ehandler, sieve_lexer_current_line(parser->lexer), __VA_ARGS__)

struct sieve_parser {
	pool_t pool;
	struct istream *input;
	
	struct sieve_error_handler *ehandler;
	
	struct sieve_lexer *lexer;
	struct sieve_ast *ast;
};

/* Forward declarations */
static bool sieve_parser_recover(struct sieve_parser *parser, enum sieve_token_type end_token);

struct sieve_parser *sieve_parser_create(int fd, struct sieve_ast *ast, struct sieve_error_handler *ehandler) 
{
	struct sieve_parser *parser;
	struct istream *stream;
  
	stream = i_stream_create_fd
		(fd, SIEVE_READ_BLOCK_SIZE, TRUE);

	if ( stream != NULL ) {
		pool_t pool = pool_alloconly_create("sieve_parser", 4096);	

		parser = p_new(pool, struct sieve_parser, 1);
		parser->input = stream;
		parser->pool = pool;
		
		parser->lexer = sieve_lexer_create(stream, ehandler);
		parser->ast = ast;
		sieve_ast_ref(ast);
		
		parser->ehandler = ehandler;
		
		return parser;
	}
	
	return NULL;
}

void sieve_parser_free(struct sieve_parser *parser)
{
	if (parser->input != NULL ) 
	  i_stream_unref(&parser->input);
	  
	sieve_ast_unref(&parser->ast);

	sieve_lexer_free(parser->lexer);

	pool_unref(&(parser->pool));
}

/* arguments = *argument [test / test-list]
 * argument = string-list / number / tag
 * string = quoted-string / multi-line   [[implicitly handled in lexer]]
 * string-list = "[" string *("," string) "]" / string         ;; if
 *   there is only a single string, the brackets are optional
 * test-list = "(" test *("," test) ")"
 * test = identifier arguments
 */
static bool sieve_parse_arguments
	(struct sieve_parser *parser, struct sieve_ast_node *node) {
	
	struct sieve_lexer *lexer = parser->lexer;
	struct sieve_ast_node *test = NULL;
	bool argument = TRUE, result = TRUE;
	
	/* Parse arguments */
	while ( argument && result ) {
		struct sieve_ast_argument *arg;
		
		switch ( sieve_lexer_current_token(lexer) ) {
		
		/* String list */
		case STT_LSQUARE:
			arg = sieve_ast_argument_stringlist_create
				(node, sieve_lexer_current_line(parser->lexer));
			sieve_lexer_skip_token(lexer);
			
			if ( sieve_lexer_current_token(lexer) == STT_STRING ) {
				sieve_ast_stringlist_add
					(arg, sieve_lexer_token_str(lexer), sieve_lexer_current_line(parser->lexer));
				sieve_lexer_skip_token(lexer);
				 
				while ( sieve_lexer_current_token(lexer) == STT_COMMA ) {
					sieve_lexer_skip_token(lexer);
				
					if ( sieve_lexer_current_token(lexer) == STT_STRING ) {
						sieve_ast_stringlist_add
							(arg, sieve_lexer_token_str(lexer), sieve_lexer_current_line(parser->lexer));
						sieve_lexer_skip_token(lexer);
					} else {
						sieve_parser_error(parser, "expecting string after ',' in string list, but found %s",
							sieve_lexer_token_string(lexer));
					
						result = sieve_parser_recover(parser, STT_RSQUARE);
						break;
					}
				}
			} else {
				sieve_parser_error(parser, "expecting string after '[' in string list, but found %s",
					sieve_lexer_token_string(lexer));
			
				result = sieve_parser_recover(parser, STT_RSQUARE);
			}
		
			if ( sieve_lexer_current_token(lexer) == STT_RSQUARE ) {
				sieve_lexer_skip_token(lexer);
			} else {
				sieve_parser_error(parser, "expecting ',' or end of string list ']', but found %s",
					sieve_lexer_token_string(lexer));
			
				if ( (result=sieve_parser_recover(parser, STT_RSQUARE)) == TRUE ) 
					sieve_lexer_skip_token(lexer);
			}
	
			break;
			
		/* Single string */
		case STT_STRING: 
			(void) sieve_ast_argument_string_create
				(node, sieve_lexer_token_str(lexer), sieve_lexer_current_line(parser->lexer));
			sieve_lexer_skip_token(lexer);
			break;
		
		/* Number */
		case STT_NUMBER:
			(void) sieve_ast_argument_number_create
				(node, sieve_lexer_token_int(lexer), sieve_lexer_current_line(parser->lexer));
			sieve_lexer_skip_token(lexer);
			break;
			
		/* Tag */
		case STT_TAG:
			(void) sieve_ast_argument_tag_create
				(node, sieve_lexer_token_ident(lexer), sieve_lexer_current_line(parser->lexer));
			sieve_lexer_skip_token(lexer);
			break;
			
		/* End of argument list, continue with tests */
		default:
			argument = FALSE;
			break;
		}
	}
	
	if ( !result ) return FALSE; /* Defer recovery to caller */
	
	/* --> [ test / test-list ] 
 	 * test-list = "(" test *("," test) ")"
	 * test = identifier arguments
	 */
	switch ( sieve_lexer_current_token(lexer) ) {

	/* Single test */
	case STT_IDENTIFIER:
		test = sieve_ast_test_create
			(node, sieve_lexer_token_ident(lexer), sieve_lexer_current_line(parser->lexer));
		sieve_lexer_skip_token(lexer);
		
		/* Parse test arguments, which may include more tests (recurse) */
		if ( !sieve_parse_arguments(parser, test) ) {
			return FALSE; /* Defer recovery to caller */
		}
		
		break;
		
	/* Test list */
	case STT_LBRACKET:	
		sieve_lexer_skip_token(lexer);
		
		node->test_list = TRUE;
		
		/* Test starts with identifier */
		if ( sieve_lexer_current_token(lexer) == STT_IDENTIFIER ) {
			test = sieve_ast_test_create
				(node, sieve_lexer_token_ident(lexer), sieve_lexer_current_line(parser->lexer));
			sieve_lexer_skip_token(lexer);
		
			/* Parse test arguments, which may include more tests (recurse) */
			if ( sieve_parse_arguments(parser, test) ) {
			
				/* More tests ? */
				while ( sieve_lexer_current_token(lexer) == STT_COMMA ) {
					sieve_lexer_skip_token(lexer);
					
					/* Test starts with identifier */
					if ( sieve_lexer_current_token(lexer) == STT_IDENTIFIER ) {
						test = sieve_ast_test_create
							(node, sieve_lexer_token_ident(lexer), sieve_lexer_current_line(parser->lexer));
						sieve_lexer_skip_token(lexer);
						
						/* Parse test arguments, which may include more tests (recurse) */
						if ( !sieve_parse_arguments(parser, test) ) {
							result = sieve_parser_recover(parser, STT_RBRACKET);
							break;
						}
					} else {
						sieve_parser_error(parser, "expecting test identifier after ',' in test list, but found %s",
							sieve_lexer_token_string(lexer));
										
						result = sieve_parser_recover(parser, STT_RBRACKET);
						break;
					}
				}
			} else 
				result = sieve_parser_recover(parser, STT_RBRACKET);
		} else {
			sieve_parser_error(parser, "expecting test identifier after '(' in test list, but found %s",
				sieve_lexer_token_string(lexer));
			
			result = sieve_parser_recover(parser, STT_RBRACKET);
		}
		
		/* The next token should be a ')', indicating the end of the test list
		 *   --> privious sieve_parser_recover calls try to restore this situation after
		 *       parse errors.  
		 */
 		if ( sieve_lexer_current_token(lexer) == STT_RBRACKET ) {
			sieve_lexer_skip_token(lexer);
		} else {
			sieve_parser_error(parser, "expecting ',' or end of test list ')', but found %s",
				sieve_lexer_token_string(lexer));
			
			/* Recover function tries to make next token equal to ')'. If it succeeds we need to 
			 * skip it.
			 */
			if ( (result = sieve_parser_recover(parser, STT_RBRACKET)) == TRUE ) 
				sieve_lexer_skip_token(lexer);
		}
		break;
		
	default:
		/* Not an error: test / test-list is optional
		 *   --> any errors are detected by the caller  
		 */
		break;
	} 
	
	return result;
}

/* commands = *command
 * command = identifier arguments ( ";" / block )
 * block = "{" commands "}"
 */
static bool sieve_parse_commands
	(struct sieve_parser *parser, struct sieve_ast_node *block) { 

	struct sieve_lexer *lexer = parser->lexer;
	bool result = TRUE;

	while ( sieve_lexer_current_token(lexer) == STT_IDENTIFIER ) {
		struct sieve_ast_node *command = 
			sieve_ast_command_create
				(block, sieve_lexer_token_ident(lexer), sieve_lexer_current_line(parser->lexer));
	
		/* Defined state */
		result = TRUE;		
		
		sieve_lexer_skip_token(lexer);
		
		result = sieve_parse_arguments(parser, command);
		
		/* Check whether the command is properly terminated (i.e. with ; or a new block) */
		if ( result &&
			sieve_lexer_current_token(lexer) != STT_SEMICOLON &&
			sieve_lexer_current_token(lexer) != STT_LCURLY ) {
			
			sieve_parser_error(parser, "expected end of command ';' or the beginning of a compound block '{', but found %s",
					sieve_lexer_token_string(lexer));	
			result = FALSE;
		}
		
		/* Try to recover from parse errors to reacquire a defined state */
		if ( !result ) {
			result = sieve_parser_recover(parser, STT_SEMICOLON);
		}
		
		/* Don't bother to continue if we are not in a defined state */
		if ( !result ) return FALSE;
			
		switch ( sieve_lexer_current_token(lexer) ) {
		
		/* End of the command */
		case STT_SEMICOLON:
			sieve_lexer_skip_token(lexer);
			break;
		
		case STT_LCURLY:
			sieve_lexer_skip_token(lexer);
			
			command->block = TRUE;
			
			if ( sieve_parse_commands(parser, command) ) {
			
				if ( sieve_lexer_current_token(lexer) != STT_RCURLY ) {
					sieve_parser_error(parser, "expected end of compound block '}' but found %s",
						sieve_lexer_token_string(lexer));
					result = sieve_parser_recover(parser, STT_RCURLY);				
				} else 
					sieve_lexer_skip_token(lexer);
			} else 	if ( (result = sieve_parser_recover(parser, STT_RCURLY)) == TRUE ) 
				sieve_lexer_skip_token(lexer);

			break;
			
		default:
			/* Recovered previously, so this cannot happen */
			i_unreached();
		}
	}
		
	return result;
}

bool sieve_parse(struct sieve_parser *parser) 
{
	/* Scan first token */
	sieve_lexer_skip_token(parser->lexer);

	if ( sieve_parse_commands(parser, parser->ast->root) ) { 
	  if ( sieve_lexer_current_token(parser->lexer) != STT_EOF ) { 
			sieve_parser_error(parser, "unexpected token %s found at (the presumed) end of file",
				sieve_lexer_token_string(parser->lexer));
				
			return FALSE;				
		}
		
		return TRUE;
	} 
	
	return FALSE;
}	

/* Error recovery:
 *   To continue parsing after an error it is important to find the next parsible item in the 
 *   stream. The recover function skips over the remaining garbage after an error. It tries 
 *   to find the end of the failed syntax structure and takes nesting of structures into account.
 *   
 */

/* Assign useful names to priorities for readability */ 
enum sieve_grammatical_prio {
  SGP_BLOCK = 3,
  SGP_COMMAND = 2,
  SGP_TEST_LIST = 1,
  SGP_STRING_LIST = 0,
  
  SGP_OTHER = -1
};

static __inline__ enum sieve_grammatical_prio __get_token_priority(enum sieve_token_type token) {
	switch ( token ) {
	case STT_LCURLY:
	case STT_RCURLY: 
		return SGP_BLOCK;
	case STT_SEMICOLON: 
		return SGP_COMMAND;
	case STT_LBRACKET:
	case STT_RBRACKET: 
		return SGP_TEST_LIST;
	case STT_LSQUARE:
	case STT_RSQUARE: 
		return SGP_STRING_LIST;
	default:
		break;
	}
	
	return SGP_OTHER;
}

static bool sieve_parser_recover(struct sieve_parser *parser, enum sieve_token_type end_token) 
{
	/* The tokens that begin/end a specific block/command/list in order 
 	 * of ascending grammatical priority.
 	 */ 
 	static const enum sieve_token_type begin_tokens[4] = 
 		{ STT_LSQUARE, STT_LBRACKET, STT_NONE, STT_LCURLY };
	static const enum sieve_token_type end_tokens[4] = 
		{ STT_RSQUARE, STT_RBRACKET, STT_SEMICOLON, STT_RCURLY};

	struct sieve_lexer *lexer = parser->lexer;
	int nesting = 1;
	enum sieve_grammatical_prio end_priority = __get_token_priority(end_token);
			
	i_assert( end_priority != SGP_OTHER );
			
	while ( sieve_lexer_current_token(lexer) != STT_EOF && 
		__get_token_priority(sieve_lexer_current_token(lexer)) <= end_priority ) {
			
		if ( sieve_lexer_current_token(lexer) == begin_tokens[end_priority] ) {
			nesting++;	
			sieve_lexer_skip_token(lexer);
			continue;
		}
		
		if ( sieve_lexer_current_token(lexer) == end_tokens[end_priority] ) {
			nesting--;
			
			if ( nesting == 0 ) {
				/* Next character is the end */
				return TRUE; 
			}
		}
		
		sieve_lexer_skip_token(lexer);
	}
	
	/* Special case: COMMAND */
	if (end_token == STT_SEMICOLON && sieve_lexer_current_token(lexer) == STT_LCURLY)
		return TRUE;
	
	/* End not found before eof or end of surrounding grammatical structure 
	 */
	return FALSE; 
}



