require "imapflags";
require "fileinto";

if header :contains "from" "boss@frobnitzm.example.edu" {
	setflag "\\Flagged";
	fileinto "INBOX.From Boss";
}

if header :contains "Disposition-Notification-To" "mel@example.com" {
	addflag "$MDNRequired";
}

if header :contains "from" "imap@cac.washington.example.edu" {
	removeflag "$MDNRequired";
	fileinto "INBOX.imap-list";
}
