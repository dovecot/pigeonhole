require "special-use";
require "variables";
require "encoded-character";

set "mailbox" "${hex:ff}rop";
if specialuse_exists "${mailbox}" "\\Sent" {
	keep;
}

