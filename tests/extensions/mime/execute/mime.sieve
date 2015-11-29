require "mime";
require "foreverypart";
require "variables";

if header :contains :mime "Content-Type" "text/plain" {
	discard;
}
if header :mime :type "Content-Type" "text" {
	discard;
}
if header :mime :subtype "Content-Type" "plain" {
	discard;
}
if header :mime :contenttype "Content-Type" "text/plain" {
	discard;
}
if header :mime :param ["frop", "friep"] "Content-Type" "frml" {
	discard;
}
if header :anychild :contains :mime "Content-Type" "text/plain" {
	discard;
}
if header :mime :anychild :type "Content-Type" "text" {
	discard;
}
if header :mime :subtype :anychild "Content-Type" "plain" {
	discard;
}
if header :anychild :mime :contenttype "Content-Type" "text/plain" {
	discard;
}
if header :mime :param ["frop", "friep"] :anychild "Content-Type" "frml" {
	discard;
}

foreverypart {
	foreverypart {
		if header :contains :mime "Content-Type" "text/plain" {
			discard;
		}
		if header :mime :type "Content-Type" "text" {
			discard;
		}
		if header :mime :subtype "Content-Type" "plain" {
			discard;
		}
		if header :mime :contenttype "Content-Type" "text/plain" {
			discard;
		}
		if header :mime :param ["frop", "friep"] "Content-Type" "frml" {
			discard;
		}
		if header :anychild :contains :mime "Content-Type" "text/plain" {
			discard;
		}
		if header :mime :anychild :type "Content-Type" "text" {
			discard;
		}
		if header :mime :subtype :anychild "Content-Type" "plain" {
			discard;
		}
		if header :anychild :mime :contenttype "Content-Type" "text/plain" {
			discard;
		}
		if header :mime :param ["frop", "friep"] :anychild "Content-Type" "frml" {
			discard;
		}
	}
}
