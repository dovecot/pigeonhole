if address :is ["from", "to", "cc"] "sirius@drunksnipers.com {
  keep;
} elsif size :under 4000 {
  stop;
} else {
  discard;
}
