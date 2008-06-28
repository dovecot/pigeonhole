require "vnd.dovecot.testsuite";

test "Message environment test" {
	test_set "message" text:
From: sirius@rename-it.nl
To: nico@vestingbar.nl
Subject: Frop!

Frop!
.
	;
	test_set "envelope.from" "stephan@rename-it.nl";

	if not header :contains "from" "rename-it.nl" {
		test_fail "Message data not set properly.";
	}

	test_set "message" text:
From: nico@vestingbar.nl
To: stephan@zuiphol.nl
Subject: Friep!

Friep!
.
	;
	test_set "envelope.from" "stephan@rename-it.nl";

	if not header :is "from" "nico@vestingbar.nl" {
    	test_fail "Message data not set properly.";
	} 

	keep;
}

