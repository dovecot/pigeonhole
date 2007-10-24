size;

if size {
}

if size 45 {
	discard;
}

if size :over 34K {
	stop;
}

if size :under 34M {
	stop;
}

if size :under :over 34 {
	keep;
}

if size :over :over 45M {
	if size 34M :over {
		stop;
	}
}
