require "date";
require "index";

# Not an error
if header :last :index 2 "to" "ok" { }

# Not an error
if header :index 444 :last "to" "ok" { }

# 1: missing argument
if header :index "to" "ok" {}

# 2: missing argument
if header :index :last "to" "ok" {}

# 3: erroneous string argument
if header :index "frop" "to" "ok" {}

# 4: last without index
if header :last "to" "ok" {}
