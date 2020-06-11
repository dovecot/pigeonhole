require "mailbox";
require "variables";
require "encoded-character";

set "mailbox" "${hex:ff}rop";
if mailboxexists "${mailbox}" {
	keep;
}

