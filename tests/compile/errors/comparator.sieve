/*
 * Address part errors
 *
 * Total errors: 5 (+1 = 6)
 */

# 1: No argument
if address :comparator { }

# 2: Number argument
if address :comparator 1 "from" "frop" { }

# 3: String list argument
if address :comparator ["a", "b"] "from" "frop" { }

# 4: Unknown tag
if address :comparator :frop "from" "frop" { }

# 5: Known tag
if address :comparator :all "from" "frop" { }

