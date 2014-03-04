require "duplicate";

# Used as a command
duplicate;

# Used with no argument (not an error)
if duplicate {}

# Used with string argument
if duplicate "frop" { }

# Used with numner argument
if duplicate 23423 { }

# Used with numer argument
if duplicate ["frop"] { }

# Used with unknown tag
if duplicate :test "frop" { }

# Bad :header parameter
if duplicate :header 23 {}

# Bad :uniqueid parameter
if duplicate :uniqueid 23 {}

# Bad :handle parameter
if duplicate :handle ["a", "b", "c"] {}

# Bad seconds parameter
if duplicate :seconds "a" {}

# Missing :header parameter
if duplicate :header {}

# Missing :uniqueid parameter
if duplicate :uniqueid {}

# Missing :handle parameter
if duplicate :handle {}

# Missing seconds parameter
if duplicate :seconds {}

# :last with a parameter
if duplicate :last "frop" {}

# :last as :seconds parameter
if duplicate :seconds :last {}

# Conflicting tags
if duplicate :header "X-Frop" :uniqueid "FROP!" { }


