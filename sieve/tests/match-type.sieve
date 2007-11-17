if address :is :comparator "i;octet" :domain "from" "STEPHAN" {
	discard;

	if address :contains :domain :comparator "i;octet" "from" "drunksnipers.com" {
		keep;
	}
	stop;
}

keep;
