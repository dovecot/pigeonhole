if address :contains "to" "vestingbar" {
	redirect "stephan@example.com";
	keep;
} else {
	discard;
}

redirect "stephan@rename-it.nl";
redirect "nico@example.nl";
redirect "stephan@example.com";

keep;
discard;
