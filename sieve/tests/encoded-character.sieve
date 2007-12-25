require "encoded-character";
require "fileinto";
require "reject";

if address :contains "from" "idiot.com" {
	reject "You are an ${hex: 69 64 69 6F 74}.";
} elsif header :contains "subject" "idiot" {
	fileinto "INBOX.${hex: 49 44 49 4F 54}";
} else {
	fileinto "INBOX.${unicode: 0052 00E6}vh${unicode: 00F8 006C}";
}
