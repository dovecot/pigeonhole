require "fileinto";

if address :contains "from" "frop" {
	fileinto "junk";
	stop;
}

redirect "me@example.com";


