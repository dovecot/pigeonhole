if address :comparator "i;ascii-casemap" :localpart "from" "STEPHAN" {
	discard;

	if address :domain :comparator "i;octet" "from" "drunksnipers.com" {
		keep;
	}
	stop;
}

keep;
