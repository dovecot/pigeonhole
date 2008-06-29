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
if header :matches "x-bullshit" "33333\\?\\?\\??" {
	fileinto "H";
}
if header :matches "x-bullshit" "33333*\\?\\??" {
    fileinto "I";
}
if header :matches "x-bullshit" "*\\?\\?\\?a" {
    fileinto "J";
}
if header :matches "x-bullshit" "*3333\\?\\?\\?a" {
    fileinto "K";
}

fileinto "SHOULD NOT MATCH";
if address :matches "from" "*@d*kn*ers.com" {
	fileinto "NA";
}
if address :matches "from" "stepan+sieve@drunksnipers.*" {
	fileinto "NB";
}
if address :matches "from" "*+sieve@drunksnipers.om" {
	fileinto "NC";
}
if address :matches "from" "stephan+sieve?drunksipers.com" {
	fileinto "ND";
}
if address :matches "from" "?tephan+sievedrunksnipers.com" {
	fileinto "NE";
}
if address :matches "from" "sephan+sieve@drunksnipers.co?" {
	fileinto "NF";
}
if address :matches "from" "?t?phan?sieve?dunksnip?rs.co?" {
	fileinto "NG";
}
if header :matches "x-bullshit" "33333\\?\\?\\?" {
	fileinto "NH";
}
if header :matches "x-bullshit" "33333\\?\\?\\?aa" {
    fileinto "NI";
}
if header :matches "x-bullshit" "\\*3333\\?\\?\\?a" {
    fileinto "NJ";
}
if header :matches "x-bullshit" "\\f3333\\?\\?\\?a" {
    fileinto "NK";
}

