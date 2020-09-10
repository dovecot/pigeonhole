require "imap4flags";

# 1-10: Used incorrectly as a command vs test
if setflag {}
if addflag {}
if removeflag {}
if setflag;
if addflag;
if removeflag;
hasflag;

# 11-19: Used with no argument
setflag;
addflag;
removeflag;
if hasflag {}
if hasflag;
if not hasflag {}
if not hasflag;

# Used with one string argument (OK)
setflag "frop";
addflag "frop";
removeflag "frop";
if hasflag "frop" {}

# 20-25: Used with one number argument
setflag 234234;
addflag 23423;
removeflag 234234;
if hasflag 234234 {}
if hasflag 234234;

# Used with one string list argument (OK)
setflag ["frop"];
addflag ["frop"];
removeflag ["frop"];
if hasflag ["frop"] {}

# 26-30: Used with unknown tag
setflag :frop "frop";
addflag :frop "frop";
removeflag :frop "frop";
if hasflag :frop "frop" {}
if hasflag :frop "frop";

