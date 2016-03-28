require "enotify";
require "ihave";

if ihave "notify" {
	notify :options "frop@frop.example.org";
}
