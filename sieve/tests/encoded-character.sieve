require "encoded-character";
require "fileinto";
require "reject";

if address :contains "from" "idiot.com" {
	reject "You are an ${hex: 69 64 69 6F 74}.";
} else {
	fileinto "INBOX.${hex: 49 44 49 4F 54}";
}
