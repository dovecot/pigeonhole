require ["comparator-i;ascii-numeric", "relational", "fileinto"];

if header :value "eq" :comparator "i;ascii-numeric" "x-spam-score" "2" {
	fileinto "INBOX.fail";
} else {
	fileinto "INBOX.succeed";
}

if header :value "lt" :comparator "i;ascii-numeric" "x-spam-score" "2" {
	fileinto "INBOX.fail";
} else {
	fileinto "INBOX.succeed";
}

if header :value "lt" :comparator "i;ascii-numeric" "x-spam-score" "2" {
	fileinto "INBOX.fail";
} else {
    fileinto "INBOX.succeed";
}

if header :value "le" :comparator "i;ascii-numeric" "x-spam-score" "300" {
    fileinto "INBOX.succeed";
} else {
    fileinto "INBOX.fail";
}

if header :value "le" :comparator "i;ascii-numeric" "x-spam-score" "302" {
    fileinto "INBOX.succeed";
} else {
    fileinto "INBOX.fail";
}

if header :value "le" :comparator "i;ascii-numeric" "x-spam-score" "00302" {
    fileinto "INBOX.succeed";
} else {
    fileinto "INBOX.fail";
}

if header :count "ne" :comparator "i;ascii-numeric" "to" "2" {
	fileinto "INBOX.fail";
} else {
    fileinto "INBOX.succeed";
}

if header :count "ge" :comparator "i;ascii-numeric" "to" "2" {
	fileinto "INBOX.succeed";
} else {
    fileinto "INBOX.fail";
}

if header :count "ge" :comparator "i;ascii-numeric" "to" "002" {
    fileinto "INBOX.succeed";
} else {
    fileinto "INBOX.fail";
}

keep;
