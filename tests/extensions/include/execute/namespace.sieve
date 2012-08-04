require "vnd.dovecot.testsuite";
require "include";
require "variables";

set "global.a" "none";
include :personal "namespace";

if string "${global.a}" "none" {
	test_fail "personal script not executed";
}

if not string "${global.a}" "personal" {
	test_fail "executed global instead of personal script: ${global.a}";
}

set "global.a" "none";
include :global "namespace";

if string "{global.a}" "none" {
	test_fail "global script not executed";
}

if not string "${global.a}" "global" {
	test_fail "executed personal instead of global script: ${global.a}";
}

