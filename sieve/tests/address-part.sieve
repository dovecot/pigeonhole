if address :comparator "i;octet" :domain "from" "STEPHAN" {
	discard;

	if address :domain :comparator "i;octet" "from" "drunksnipers.com" {
		keep;
	}
	stop;
}

keep;
