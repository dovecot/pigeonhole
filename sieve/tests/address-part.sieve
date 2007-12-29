if address :comparator "i;octet" :domain "from" "STEPHAN" {
	discard;

	if address :localpart :comparator "i;octet" "from" "drunksnipers.com" {
		keep;
	}
	stop;
}

keep;
