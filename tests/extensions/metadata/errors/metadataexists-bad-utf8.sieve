require "mboxmetadata";
require "variables";
require "encoded-character";

set "mailbox" "${hex:ff}rop";
if metadataexists "${mailbox}" ["/private/frop", "/shared/friep"] {
	keep;
}

