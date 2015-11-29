require "mime";

## Address

# No error
if address :contains :mime "To" "frop@example.com" {
	discard;
}

# No error
if address :anychild :contains :mime "To" "frop@example.com" {
	discard;
}

# 1: Bare anychild option
if address :anychild "To" "frop@example.com" {
	discard;
}

# 2: Inappropriate option
if address :mime :anychild :type "To" "frop@example.com" {
	discard;
}

# 3: Inappropriate option
if address :mime :anychild :subtype "To" "frop@example.com" {
	discard;
}

# 4: Inappropriate option
if address :mime :anychild :contenttype "To" "frop@example.com" {
	discard;
}

# 5: Inappropriate option
if address :mime :anychild :param "frop" "To" "frop@example.com" {
	discard;
}
