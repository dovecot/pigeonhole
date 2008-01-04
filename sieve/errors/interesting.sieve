require ["fileinto"];
require ["copy", "relational"];
require "envelope";
require "regex";

va

if header :is "To" "Stephan \"Nico\" Bosch <nico@voorbeeld.nl>" {
	fileinto "INBOX.stephan";	
} elsif header :matches "To" "*@voorbeeld.nl" {
	fileinto "INBOX.vestingbar";
}

if envelope :isnot :comperator "i;ascii-casemap" :localpart "From" "nico" {
 	discard;
}

if :disabled true {
	break;
}

if header :comparator "i;octet" :is :comparator "i;ascii-casemap" {
	frop;
}
