require "envelope";

if envelope :isnot :comparator "i;ascii-casemap" :localpart "From" "nico" {
	discard;
}

if envelope :is :comparator 45 :localpart "From" "nico" {
	discard;
}

if true :comparator "i;ascii-numeric" {
  	keep;
}

if envelope :is :comparator "i;ascii-numeric" :localpart "From" 45 {
	discard;
}

if envelope :is :comparator "i;ascii-numeric" :localpart 45 "nico" {
	discard;
}

if envelope :is :comparator "i;ascii-numeric" :localpart "From" :tag {
	discard;
}

if envelope :is :comparator "i;ascii-numeric" :localpart "From" {
	discard;
}

if envelope :is :comparator "i;ascii-numeric" :localpart {
	discard;
}

if envelope :localpart :is :comparator "i;ascii-casemap" "frop" ["frop", "frop"] {
	discard;
}

if envelope :localpart :hufter :is :comparator "i;ascii-casemap" "frop" ["frop", "frop"] {
        discard;
}
