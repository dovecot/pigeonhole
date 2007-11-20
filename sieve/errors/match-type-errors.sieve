if address :contains :is :comparator "i;ascii-casemap" :localpart "from" "STEPHAN" {
	discard;

	if address :contains :domain :comparator "i;octet" :matches "from" "drunksnipers.com" {
		keep;
	}
	stop;
}

keep;
