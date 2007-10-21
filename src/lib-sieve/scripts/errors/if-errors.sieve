if "frop" true;
if true;
if {
  keep;
}
if ( false, false, true ) {
  keep;
}
if [ "false", "false", "false" ] {
  stop;
}
elsi;
elsif true {
  keep;
}
elsif true {
  keep;
}
if true {
  keep;
} else {
  stop;
} elsif {
  stop;
}
stop;
else {
  keep;
}
