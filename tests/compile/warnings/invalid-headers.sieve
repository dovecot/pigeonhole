if header "from:" "frop@example.org" {
	stop;
}

if address "from:" "frop@example.org" {
	stop;
}

if exists "from:" {
	stop;
}
