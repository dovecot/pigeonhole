require "mime";

## Header

# No error
if header :contains :mime "Content-Type" "text/plain" {
	discard;
}

# No error
if header :mime :type "Content-Type" "text" {
	discard;
}

# No error
if header :mime :subtype "Content-Type" "plain" {
	discard;
}

# No error
if header :mime :contenttype "Content-Type" "text/plain" {
	discard;
}

# No error
if header :mime :param ["frop", "friep"] "Content-Type" "frml" {
	discard;
}

# No error
if header :anychild :contains :mime "Content-Type" "text/plain" {
	discard;
}

# No error
if header :mime :anychild :type "Content-Type" "text" {
	discard;
}

# No error
if header :mime :subtype :anychild "Content-Type" "plain" {
	discard;
}

# No error
if header :anychild :mime :contenttype "Content-Type" "text/plain" {
	discard;
}

# No error
if header :mime :param ["frop", "friep"] :anychild "Content-Type" "frml" {
	discard;
}

# 1: Bare anychild option
if header :anychild "Content-Type" "frml" {
	discard;
}

# 2: Bare mime option
if header :type "Content-Type" "frml" {
	discard;
}

# 3: Bare mime option
if header :subtype "Content-Type" "frml" {
	discard;
}

# 4: Bare mime option
if header :contenttype "Content-Type" "frml" {
	discard;
}

# 5: Bare mime option
if header :param "frop" "Content-Type" "frml" {
	discard;
}

# 6: Multiple option tags
if header :mime :type :subtype "Content-Type" "frml" {
	discard;
}

# 7: Bad param argument
if header :mime :param 13 "Content-Type" "frml" {
	discard;
}

# 8: Missing param argument
if header :mime :param :anychild "Content-Type" "frml" {
	discard;
}

# 9: Missing param argument
if header :mime :param :frop "Content-Type" "frml" {
	discard;
}


