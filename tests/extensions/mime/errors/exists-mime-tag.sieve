require "mime";

## Exists

# No error
if exists :mime "To" {
	discard;
}

# No error
if exists :anychild :mime "To" {
	discard;
}

# 1: Inappropriate option
if exists :anychild "To" {
	discard;
}

# 2: Inappropriate option
if exists :mime :type "To" {
	discard;
}

# 3: Inappropriate option
if exists :mime :subtype "To" {
	discard;
}

# 4: Inappropriate option
if exists :mime :contenttype "To" {
	discard;
}

# 5: Inappropriate option
if exists :mime :param ["frop", "friep"] "To" {
	discard;
}





