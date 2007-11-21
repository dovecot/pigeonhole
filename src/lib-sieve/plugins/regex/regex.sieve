require "regex";

if address :regex :comparator "i;ascii-casemap" "from" [
	"stephan(\\+.*)?@rename-it\\.com", 
	"stephan(\\+.*)?@drunksnipers\\.com" 
	] {
	keep;
	stop;
}

discard;
