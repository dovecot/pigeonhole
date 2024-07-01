require "extlists";
require "envelope";

if address :list :comparator "i;ascii-casemap" "from" ":addrbook:personal?label=Family" {
	redirect :list "tag:example.com,2010-05-28:mylist";
	stop;
}
