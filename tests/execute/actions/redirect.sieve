if address :contains "to" "vestingbar" {
	/* #1 */
	redirect "stephan@example.com";
	
	/* #2 */
	keep;
}

/* #3 */
redirect "stephan@rename-it.nl";

/* #4 */
redirect "nico@example.nl";

/* Duplicates */
redirect "Stephan Bosch <stephan@example.com>";
keep;
