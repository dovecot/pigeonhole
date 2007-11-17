require ["comparator-i;ascii-numeric", "relational"];

if header :value "gt" :comparator "i;ascii-numeric" "x-spam-score" "2" {
	discard;
	stop;
}

keep;
