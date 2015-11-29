require "foreverypart";

# 1: No block
foreverypart;

# 2: Spurious tag
foreverypart :tag { }

# 3: Spurious tests
foreverypart true { }

# 4: Spurious tests
foreverypart anyof(true, false) { }

# 5: Bare string
foreverypart "frop" { }

# 6: Bare string-list
foreverypart ["frop", "friep"] { }

# 7: Several bad arguments
foreverypart 13 ["frop", "friep"] { }

# 8: Spurious additional tag
foreverypart :name "frop" :friep { }

# 9: Spurious additional string
foreverypart :name "frop" "friep" { }

# 10: Bad name
foreverypart :name 13 { }

# 11: Bad name
foreverypart :name ["frop", "friep"] { }

# No error
foreverypart { keep; }

# No error
foreverypart :name "frop" { keep; }

# No error
foreverypart :name "frop" { foreverypart { keep; } }


