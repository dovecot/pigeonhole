require "fileinto";

if address :contains "to" "vestingbar" {
#	redirect "stephan@example.com";
#	fileinto "INBOX.vestingbar";
	keep;
} else {
	discard;
}

#redirect "stephan@rename-it.nl";
#redirect "nico@example.nl";
#redirect "stephan@example.com";

#fileinto "INBOX.frop";
#fileinto "INBOX";
keep;
discard;
