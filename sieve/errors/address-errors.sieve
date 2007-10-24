if address :isnot :comparator "i;ascii-casemap" :localpart "From" "nico" {
	discard;
}

if address :is :comparator 45 :localpart "From" "nico" {
	discard;
}

if true :comparator "i;ascii-numeric" {
  	keep;
}

if address :is :comparator "i;ascii-numeric" :localpart "From" 45 {
	discard;
}

if address :is :comparator "i;ascii-numeric" :localpart 45 "nico" {
	discard;
}

if address :is :comparator "i;ascii-numeric" :localpart "From" :tag {
	discard;
}

if address :is :comparator "i;ascii-numeric" :localpart "From" {
	discard;
}

if address :is :comparator "i;ascii-numeric" :localpart {
	discard;
}

if address :localpart :is :comparator "i;ascii-casemap" "frop" ["frop", "frop"] {
	discard;
}

if address :localpart :hufter :is :comparator "i;ascii-casemap" "frop" ["frop", "frop"] {
        discard;
}
