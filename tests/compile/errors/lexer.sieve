/*
 * Lexer tests
 *
 * Total errors: 8 (+1 = 9)
 */

/*
 * Number limits
 */

# 1: Number too large
if size :under 18446744073709551617 {
	stop;
}

# 2: Number too large
if size :under 18446744073709551616 {
	stop;
}

# 3: Number too large
if size :over 180143985094819840k {
	stop;
}

# 4: Number too large
if size :over 1006622342342296M {
	stop;
}

# 5: Number too large
if size :over 34359738368G {
	stop;
}

# 6: Number far too large
if size :over 49834598293485814273947921734981723971293741923 {
	stop;
}

# Not an error
if size :under 18446744073709551615 {
	stop;
}

# Not an error
if size :under 18446744073709551614 {
	stop;
}

# Not an error
if size :under 800G {
	stop;
}

/*
 * Identifier limits
 */

# 7: Identifier too long
if this_is_a_rediculously_long_test_name {
	stop;
}

# 8: Identifier way too long
if test :this_is_an_even_more_rediculously_long_tagged_argument_name {
	stop;
}
