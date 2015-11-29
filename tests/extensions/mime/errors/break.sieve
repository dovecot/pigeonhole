require "foreverypart";

foreverypart :name "frop" {
	# 1: Spurious tag
	break :tag;

	# 2: Spurious tests
	break true;

	# 3: Spurious tests
	break anyof(true, false);

	# 4: Bare string
	break "frop";

	# 5: Bare string-list
	break ["frop", "friep"];

	# 6: Several bad arguments
	break 13 ["frop", "friep"];

	# 7: Spurious additional tag
	break :name "frop" :friep;

	# 8: Spurious additional string
	break :name "frop" "friep";

	# 9: Bad name
	break :name 13;

	# 10: Bad name
	break :name ["frop", "friep"];

	# No error
	break;

	# No error
	break :name "frop";

	# No error
	if exists "frop" {
		break;
	}

	# No error
	if exists "frop" {
		break :name "frop";
	}

	# No error	
	foreverypart {
		break :name "frop";
	}

	# No error
	foreverypart :name "friep" {
		break :name "frop";
	}

	# No error
	foreverypart :name "friep" {
		break :name "friep";
	}

	# No error
	foreverypart :name "friep" {
		break;
	}

	# No error	
	foreverypart {
		if exists "frop" {
			break :name "frop";
		}
	}

	# No error
	foreverypart :name "friep" {
		if exists "frop" {
			break :name "frop";
		}
	}

	# No error
	foreverypart :name "friep" {
		if exists "frop" {
			break :name "friep";
		}
	}

	# No error
	foreverypart :name "friep" {
		if exists "frop" {
			break;
		}
	}
}

# 11: Outside loop
break; 

# 12: Outside loop
if exists "frop" {
	break;
}

# 13: Outside loop
break :name "frop";

# 14: Outside loop
if exists "frop" {
	break :name "frop";
}

# 15: Bad name
foreverypart {
	break :name "frop";
}

# 16: Bad name
foreverypart {
	if exists "frop" {
		break :name "frop";
	}
}

# 17: Bad name
foreverypart :name "friep" {
	break :name "frop";
}

# 18: Bad name
foreverypart :name "friep" {
	if exists "frop" {
		break :name "frop";
	}
}

# 19: Bad name
foreverypart :name "friep" {
	foreverypart :name "frop" {
		break :name "frml";
	}
}

# 20: Bad name
foreverypart :name "friep" {
	foreverypart :name "frop" {
		if exists "frop" {
			break :name "frml";
		}
	}
}




