require "foreverypart";
require "include";
require "mime";

foreverypart {
	if header :mime :subtype "content-type" "plain" {
		break;
	}
}
