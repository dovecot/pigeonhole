require "subaddress";
require "fileinto";

if address :comparator "i;ascii-casemap" :user "from" "STEPHAN" {
	discard;

	if address :detail :comparator "i;octet" "from" "sieve" {
		keep;
		stop;
	}
	fileinto "INBOX.frop";
}

keep;
