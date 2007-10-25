if address :is ["from", "to", "cc"] "sirius@drunksnipers.com" {
  keep;
} elsif header :is ["subject"] "WrF" {
  discard;
} elsif exists "X-Hufter" {
  keep;
} elsif size :under 4000 {
  stop;
} else {
  discard;
}
