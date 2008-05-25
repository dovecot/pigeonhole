require "vnd.dovecot.testsuite";

test_set "message" text:
From: sirius@rename-it.nl
To: nico@vestingbar.nl
Subject: Frop!

Frop!
.
;

if not header :contains "from" "rename-it.nl" {
	discard;
	stop;
}

test_set "message" text:
From: nico@vestingbar.nl
To: stephan@zuiphol.nl
Subject: Friep!

Friep!
.
;

if not header :is "from" "nico@vestingbar.nl" {
    discard;
    stop;
} 

keep;


