require "mboxmetadata";
require "servermetadata";
require "encoded-character";

# 1-4: Used as a command
metadata;
metadataexists;
servermetadata;
servermetadataexists;

# 5-8: Used with no argument
if metadata {}
if metadataexists {}
if servermetadata {}
if servermetadataexists {}

# 9-11: Used with one string argument
if metadata "frop" { }
if servermetadata "frop" { }
if metadataexists "frop" { }

# 12-15: Used with one number argument
if metadata 13123123 { }
if servermetadata 123123 { }
if metadataexists 123123 { }
if servermetadataexists 123123 {}

# 16-18: Used with one string list argument
if metadata ["frop"] { }
if servermetadata ["frop"] { }
if metadataexists ["frop"] { }

# 19-22: Used with unknown tag
if metadata :frop "frop" { }
if servermetadata :frop "frop" { }
if metadataexists :frop "frop" { }
if servermetadataexists :frop "frop" {}

# 23-26: Invalid arguments
if metadata "/private/frop" "friep" {}
if servermetadata "INBOX" "/private/frop" "friep" {}
if metadataexists 23 "/private/frop" {}
if servermetadataexists "INBOX" "/private/frop" {}

# W1-W4: Invalid annotations
if metadata "INBOX" "frop" "friep" {}
if servermetadata "frop" "friep" {}
if metadataexists "INBOX" ["/private/frop", "/friep"] { }
if servermetadataexists ["/private/frop", "/friep", "/private/friep"] { }

# W5-W6: Invalid mailbox name
if metadata "${hex:ff}rop" "/private/frop" "friep" {}
if metadataexists "${hex:ff}rop" ["/private/frop", "/shared/friep"] { }
