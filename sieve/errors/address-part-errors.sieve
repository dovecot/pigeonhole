if address :all :comparator "i;octet" :domain "from" "STEPHAN" {
	discard;

	if address :localpart :domain :comparator "i;octet" "from" "drunksnipers.com" {
		keep;
	}
	stop;
}

keep;
