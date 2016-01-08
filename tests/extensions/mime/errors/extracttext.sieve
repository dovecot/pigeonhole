require "extracttext";
require "variables";
require "foreverypart";

# 1: Used outside foreverypart
extracttext :first 10 "data";

foreverypart {
	# 2: Missing arguments
	extracttext;
	
	# 3: Bad arguments
	extracttext 1;

	# 4: Bad arguments
	extracttext ["frop", "friep"];

	# 5: Unknown tag
	extracttext :frop "frop";

	# 6: Invalid variable name
	extracttext "${frop}";

	# Not an error
	extracttext "\n\a\m\e";

	# 7: Trying to assign match variable
	extracttext "0";

	# Not an error
	extracttext :lower "frop";

	# 8: Bad ":first" tag
	extracttext :first "frop";

	# 9: Bad ":first" tag
	extracttext :first "frop" "friep";

	# 10: Bad ":first" tag
	extracttext :first ["frop", "friep"] "frml";
}

