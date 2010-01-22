if true {
	if true {
		keep
	}
}

if true {
	keep,
	keep
}

if true {
	if anyof(true,true,false) {
		keep;
	}
}

if true {
	if anyof(true,true,false) {
		keep;
		discard
	}
}

