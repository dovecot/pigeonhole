require "comparator-i;ascii-numeric";

if address :contains :comparator "i;ascii-casemap" :localpart "from" "STEPHAN" {
	discard;

	if address :contains :domain :comparator "i;octet" "from" "drunksnipers.com" {
		keep;
	}
	stop;
}

keep;
