require ["fileinto", "reject"];

if anyof(exists "frop", size :over 45, size :under 10, address "from" "frop@student.utwente.nl") {
	keep;
} else {
	if address "to" "sirius@joker.com" {
		reject "I dont want your email.";
	} else {
		fileinto "INBOX.Wtf";
	}
}
