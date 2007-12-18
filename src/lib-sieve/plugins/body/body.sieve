require ["body", "fileinto"];

if body :content "text/plain" :comparator "i;ascii-casemap" :contains "WERKT" {
	fileinto "WERKT";
}

if body :content "text/plain" :comparator "i;ascii-casemap" :contains "HET" {
	fileinto "HET";
}

if body :content "text/plain" :comparator "i;octet" :contains "NIET" {
	fileinto "NIET";
}
