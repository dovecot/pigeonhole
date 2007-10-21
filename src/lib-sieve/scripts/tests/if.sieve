if anyof(exists "frop", size :over 45, size :under 10, address "from" "frop@student.utwente.nl") {
	keep;
} elsif false {
	stop;
} elsif allof(
		exists ["furtp", "X-Tukker", "X-Spam"],
		anyof(
			address "frop" "frml",
			header "fruts" "friep"
		), 
		header "X-Hufter" "true", 
		exists "X-Tukker", 
		address "X-hufter" "frop") {
	keep;
	stop;
} else {
	discard;
}
