require "mboxmetadata";
require "variables";
require "encoded-character";

set "mailbox" "${hex:ff}rop";
if metadata "${mailbox}" "/private/frop" "friep" {
	keep;
}

