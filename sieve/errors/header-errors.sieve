if header :isnot :comparator "i;ascii-casemap" "From" "nico" {
	discard;
}

if header :is :comparator 45 "From" "nico" {
	discard;
}

if header :all :comparator "i;ascii-numeric" {
  	keep;
}

if header :is :comparator "i;ascii-numeric" "From" 45 {
	discard;
}

if header :is :comparator "i;ascii-numeric" 45 "nico" {
	discard;
}

if header :is :comparator "i;ascii-numeric" "From" :tag {
	discard;
}

if header :is :comparator "i;ascii-numeric" "From" {
	discard;
}

if header :is :comparator "i;ascii-numeric" {
	discard;
}

if header :is :comparator "i;ascii-casemap" "frop" ["frop", "frop"] {
	discard;
}

if header :hufter :is :comparator "i;ascii-casemap" "frop" ["frop", "frop"] {
        discard;
}

if header "frop" "frop" true {
}
