if header :comparator "i;ascii-casemap" "from" "STEPHAN@drunksnipers.com" {
	discard;

	if address :comparator "i;octet" "from" "drunksnipers.com" {
		keep;
	}
	stop;
}

keep;
