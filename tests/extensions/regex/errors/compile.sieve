require "regex";
require "comparator-i;ascii-numeric";
require "envelope";

if address :regex :comparator "i;ascii-numeric" "from" "sirius(\\+.*)?@drunksnipers\\.com" {
	keep;
	stop;
}

if address :regex "from" "sirius(+\\+.*)?@drunksnipers\\.com" {
	keep;
	stop;
}

if header :regex "from" "sirius(\\+.*)?@drunk[]snipers.com" {
    keep;
    stop;
}

if envelope :regex "from" "sirius(\\+.*)?@drunksni[]pers.com" {
    keep;
    stop;
}

discard;
