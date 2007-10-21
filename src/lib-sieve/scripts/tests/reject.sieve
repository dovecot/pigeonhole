require "reject";

if anyof(exists "frop", size :over 45, size :under 10, address "from" "frop@student.utwente.nl") {
	keep;
} else {
	reject "I dont want your email.";
}
