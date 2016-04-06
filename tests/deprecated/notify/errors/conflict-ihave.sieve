require "enotify";
require "ihave";

# 1: Conflict
if ihave "notify" {
	# 2: Syntax wrong for enotify (and not skipped in compile)
	notify :options "frop@frop.example.org";
}
