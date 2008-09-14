require "fileinto";

if address :contains "to" "vestingbar" {
	fileinto "INBOX.VB";
	stop;
}

keep;
