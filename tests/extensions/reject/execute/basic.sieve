require "reject";

if address :contains "to" "vestingbar" {
	reject "Don't send unrequested messages.";
	stop;
}

keep;
