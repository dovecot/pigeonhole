require "imap4flags";
require "fileinto";
require "relational";
require "comparator-i;ascii-numeric";
require "copy";

setflag "\\Seen";
addflag "$DSNRequired";
removeflag "$DSNRequired";

if header :contains "from" "boss@frobnitzm.example.edu" {
	setflag "\\Flagged";
	fileinto :copy "From Boss";
}

if header :contains "Disposition-Notification-To" "mel@example.com" {
	addflag "$MDNRequired";
}

if header :contains "from" "imap@cac.washington.example.edu" {
	removeflag "$MDNRequired \\Flagged \\Seen \\Deleted";
	fileinto :copy "imap-list";
}

if hasflag :count "ge" :comparator "i;ascii-numeric" "2" {
	fileinto :copy "imap-twoflags";
}

fileinto :copy :flags "MDNRequired \\Draft" "imap-draft";
