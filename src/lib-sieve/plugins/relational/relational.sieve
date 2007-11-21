require ["comparator-i;ascii-numeric", "relational"];

if header :value "eq" :comparator "i;ascii-numeric" "x-spam-score" "2" {
	discard;
	stop;
}

if header :value "lt" :comparator "i;ascii-numeric" "x-spam-score" "2" {
	discard;
	stop;
}

if header :value "lt" :comparator "i;ascii-numeric" "x-spam-score" "2" {
	discard;
	stop;
}

if header :count "ne" :comparator "i;ascii-numeric" "to" "2" {
	discard;
	stop;
}

if header :count "ge" :comparator "i;ascii-numeric" "to" "2" {
	discard;
	stop;
}

keep;
