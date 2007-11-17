require ["comparator-i;ascii-numeric", "relational"];

if header :value "gt" :comparator "i;ascii-numeric" "x-spam-score" "2" {
	discard;
	stop;
}

if header :value "ne" :comparator "i;ascii-numeric" "x-spam-score" "2" {
	discard;
	stop;
}

if header :count "lt" :comparator "i;ascii-numeric" "x-spam-score" "2" {
	discard;
	stop;
}

if header :count "eq" :comparator "i;ascii-numeric" "x-spam-score" "2" {
	discard;
	stop;
}

keep;
