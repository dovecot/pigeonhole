require "regex";
require "comparator-i;ascii-numeric";

if address :regex :comparator "i;ascii-numeric" "from" "sirius(\\+.*)?@drunksnipers\\.com" {
	keep;
	stop;
}

discard;
