require ["fileinto"];
require ["copy", "relational"];
require "envelope";
require "regex";

if header :is "To" "Stephan \"Nico\" Bosch <nico@vestingbar>" {
	fileinto "INBOX.stephan";	
} elsif header :matches "To" "*@vestingbar.nl" {
	fileinto "INBOX.vestingbar";
}

if envelope :isnot :comperator "i;ascii-casemap" :localpart "From" "nico" {
 	discard;
}

if :disabled true {
	break;
}
