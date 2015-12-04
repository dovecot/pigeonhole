require "include";
require "foreverypart";
require "mime";
require "variables";

global "in";
global "error";

foreverypart {
	set :length "la" "${in}";

	if string "${in}" "aa" {
		if not header :mime "X-Test" "BB" {
			set "error" "wrong header extracted (${la})";
			return;
		}
	} elsif string "${in}" "aaa" {
		if not header :mime "X-Test" "CC" {
			set "error" "wrong header extracted (${la})";
			return;
		}
	} elsif string "${in}" "aaaa" {
		if not header :mime "X-Test" "DD" {
			set "error" "wrong header extracted (${la})";
			return;
		}
	} elsif string "${in}" "aaaaa" {
		if not header :mime "X-Test" "EE" {
			set "error" "wrong header extracted (${la})";
			return;
		}
	} elsif string "${in}" "aaaaaaa" {
		if not header :mime "X-Test" "CC" {
			set "error" "wrong header extracted (${la})";
			return;
		}
	} elsif string "${in}" "aaaaaaaa" {
		if not header :mime "X-Test" "DD" {
			set "error" "wrong header extracted (${la})";
			return;
		}
	}
	set "in" "a${in}";
}
