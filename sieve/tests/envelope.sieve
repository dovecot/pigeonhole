require "envelope";

if envelope :domain "to" "example.com" {
	discard;
	stop;
}

if envelope :domain "from" "example.com" {
	discard;
	stop;
}

keep;

