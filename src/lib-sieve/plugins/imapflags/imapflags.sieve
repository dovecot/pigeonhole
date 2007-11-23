require "imapflags";
require "fileinto";
require "relational";
require "comparator-i;ascii-numeric";

setflag "\\Seen";
addflag "$DSNRequired";
removeflag "$DSNRequired";

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

if hasflag :count "ge" :comparator "i;ascii-numeric" "2" {
	fileinto "INBOX.imap-twoflags";
}
