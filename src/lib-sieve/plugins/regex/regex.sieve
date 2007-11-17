require "regex";

if address :regex :comparator "i;ascii-casemap" "from" "sirius(\\+.*)?@drunksnipers\\.com" {
	keep;
	stop;
}

discard;
