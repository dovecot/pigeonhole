require ["body", "fileinto"];

if body :content "text/plain" :comparator "i;ascii-casemap" :contains "WERKT" {
	fileinto "TEST";
}
