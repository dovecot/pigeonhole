require "vnd.dovecot.duplicate";

# Used as a command
duplicate;

# Used with string argument (not an error)
if duplicate "frop" { }

# Used with numer argument
if duplicate 23423 { }

# Used with numer argument
if duplicate ["frop"] { }



