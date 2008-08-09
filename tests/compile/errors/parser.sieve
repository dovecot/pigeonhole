/*
 * Parser errors
 */

# Too many arguments 
frop :this "is" "a" 2 :long "argument" "list" :and :it :should "fail" :during "parsing" :but "it" "should" "be" 
	"recoverable" "." :this "is" "a" 2 :long "argument" "list" :and :it :should "fail" :during "parsing" :but 
	"it" "should" "be" "recoverable" {
	stop;
}

# Garbage argument
friep $$$;


