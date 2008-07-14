require "fileinto";

if address :contains "to" "vestingbar" {
	redirect "stephan@example.com";
	fileinto "INBOX.vestingbar";
	keep;
} else {
	discard;
}

redirect "stephan@rename-it.nl";
redirect "nico@example.nl";
redirect "stephan@example.com";
redirect "stephan@EXAMPLE.COM";

fileinto "frop";
fileinto "FrOp";
fileinto "INBOX";
fileinto "inbox";

redirect "Stephan Bosch <stephan@example.com>";
keep;
discard;
