require "relational";
require "comparator-i;ascii-numeric";

# A semicolon in the middle of things
if address :count "eq" ;comparator "i;ascii-numeric" "to" "3" { }

# A sub-command in the middle of things
if not address :comparator "i;ascii-numeric" :value e "to" "3" { }
