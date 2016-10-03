require "special-use";
require "fileinto";

# 1
if specialuse_exists {}
# 2
if specialuse_exists 3423 {}
# 3
if specialuse_exists :frop {}
# 4
if specialuse_exists 24234 "\\Sent" {}
# 5
if specialuse_exists "frop" 32234 {}
# 6
if specialuse_exists "frop" :friep {}

# 7
if specialuse_exists "frop" {}
# 8
if specialuse_exists "frop" ["frop"] {}

# 9
fileinto :specialuse "\\frop";
# 10
fileinto :specialuse 343 "\\frop";
# 11
fileinto :specialuse :create "\\frop";
# 12
fileinto :specialuse "\\frop" 234234;

# 13
fileinto :specialuse "frop" "frop";
