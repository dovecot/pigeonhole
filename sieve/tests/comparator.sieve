if header :is :comparator "i;ascii-casemap" "from" "STEPHAN@drunksnipers.com" {
	discard;

	if address :is :comparator "i;octet" :domain "from" "drunksnipers.com" {
		keep;
	}
	stop;
}

keep;
