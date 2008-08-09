/*
 * Lexer tests
 *
 * Total errors: 7 (+1 = 8)
 */ 
# Number too large
if size :under 4294967300 {
	stop;
}

# Number too large
if size :under 4294967296 {
	stop;
}

# Number too large
if size :over 35651584k {
	stop;
}

# Number too large
if size :over 34816M {
	stop;
}

# Number too large
if size :over 34G {
	stop;
}

# Number too large
if size :over 4G {
	stop;
}

# Number far too large
if size :over 49834598293485814273947921734981723971293741923 {
	stop;
}

# Not an error
if size :under 4294967295 {
	stop;
}

# Not an error
if size :under 4294967294 {
	stop;
}

# Not an error
if size :under 1G {
	stop;
}
