require "imap4flags";
require "fileinto";
require "relational";
require "comparator-i;ascii-numeric";
require "variables";

setflag "frop" "\\Seen";
addflag "frop" "$DSNRequired";
removeflag "friep" "$DSNRequired";

if header :contains "from" "boss@frobnitzm.example.edu" {
	setflag "frop" "\\Flagged";
	fileinto "From Boss";
}

if header :contains "Disposition-Notification-To" "mel@example.com" {
	addflag "frml" "$MDNRequired";
}

if header :contains "from" "imap@cac.washington.example.edu" {
	removeflag "frop" "$MDNRequired \\Flagged \\Seen \\Deleted";
	fileinto "imap-list";
}

if hasflag :count "ge" :comparator "i;ascii-numeric" "frop" "2" {
	fileinto "imap-twoflags";
}

fileinto :flags "\\Seen MDNRequired \\Draft" "INBOX";
