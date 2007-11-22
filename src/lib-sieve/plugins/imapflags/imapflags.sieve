require "imapflags";
require "fileinto";

if header :contains "from" "boss@frobnitzm.example.edu" {
	setflag "flagvar" "\\Flagged";
#	fileinto :flags "${flagvar}" "INBOX.From Boss";
}

if header :contains "Disposition-Notification-To" "mel@example.com" {
	addflag "flagvar" "$MDNRequired";
}

if header :contains "from" "imap@cac.washington.example.edu" {
	removeflag "flagvar" "$MDNRequired";
#	fileinto :flags "${flagvar}" "INBOX.imap-list";
}
