require "subaddress";
require "fileinto";

if address :comparator "i;ascii-casemap" :user "from" "STEPHAN" {
	discard;

	if address :domain :comparator "i;octet" "from" "drunksnipers.com" {
		keep;
	}
	stop;
}

keep;
