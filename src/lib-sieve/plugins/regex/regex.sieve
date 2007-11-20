require "regex";

if address :regex :comparator "i;ascii-casemap" "from" "stephan(\\+.*)?@drunksnipers\\.com" {
	keep;
	stop;
}

discard;
