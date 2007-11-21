require "fileinto";

fileinto "SHOULD MATCH";
if address :matches "from" "*@d*ksn*ers.com" {
	fileinto "A";
}
if address :matches "from" "stephan+sieve@drunksnipers.*" {
	fileinto "B";
}
if address :matches "from" "*+sieve@drunksnipers.com" {
	fileinto "C";
}
if address :matches "from" "stephan+sieve?drunksnipers.com" {
	fileinto "D";
}
if address :matches "from" "?tephan+sieve@drunksnipers.com" {
	fileinto "E";
}
if address :matches "from" "stephan+sieve@drunksnipers.co?" {
	fileinto "F";
}
if address :matches "from" "?t?phan?sieve?drunksnip?rs.co?" {
	fileinto "G";
}
if address :matches "x-bullshit" "33333\\?\\?\\??" {
	fileinto "H";
}

fileinto "SHOULD NOT MATCH";
if address :matches "from" "*@d*kn*ers.com" {
	fileinto "A";
}
if address :matches "from" "stepan+sieve@drunksnipers.*" {
	fileinto "B";
}
if address :matches "from" "*+sieve@drunksnipers.om" {
	fileinto "C";
}
if address :matches "from" "stephan+sieve?drunksipers.com" {
	fileinto "D";
}
if address :matches "from" "?tephan+sievedrunksnipers.com" {
	fileinto "E";
}
if address :matches "from" "sephan+sieve@drunksnipers.co?" {
	fileinto "F";
}
if address :matches "from" "?t?phan?sieve?dunksnip?rs.co?" {
	fileinto "G";
}
if address :matches "x-bullshit" "33333\\?\\??" {
	fileinto "H";
}

