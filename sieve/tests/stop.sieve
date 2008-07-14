require "fileinto";

if address :contains "from" "sieve" {
	fileinto "junk";
	stop;
}

redirect "me@example.com";


