require "mailbox";
require "fileinto";
require "encoded-character";

# 1
if mailboxexists {}
# 2
if mailboxexists 3423 {}
# 3
if mailboxexists :frop {}
# 4
if mailboxexists 24234 "\\Sent" {}
# 5
if mailboxexists "frop" 32234 {}
# 6
if mailboxexists "frop" :friep {}

if mailboxexists "frop" {}
if mailboxexists ["frop", "friep"] {}

# W:1
if mailboxexists "${hex:ff}rop" {}
# W:2
if mailboxexists ["frop", "${hex:ff}riep"] {}

# 7
if mailboxexists "frop" ["frop"] {}

# 8
fileinto :create 343 "frop";
# 9
fileinto :create :frop "frop";
# 10
fileinto :create 234234;

fileinto :create "frop";

# 11
fileinto :create "${hex:ff}rop";


