require "variables";
require "fileinto";

set "match1" "Test of general stupidity";

# Test 1
if string :matches "${match1}" "Test of *" {
	fileinto "TEST 1: ${1}";
} else {
	fileinto "FAILED 1: ${match1}";
	stop;
}

# Test 2
if string :matches "${match1}" "of *" {
	fileinto "FAILED 2: ${match1}";
	stop;
} else {
	fileinto "TEST 2: OK";
}

set "match2" "toptoptop";

# Test 3
if string :matches "${match2}" "*top" {
	fileinto "TEST 3: ${1}";
} else {
	fileinto "FAILED 3: ${match2}";
	stop;
}

set "match3" "ik ben een tukker met grote oren en een lelijke broek.";

# Test 4
if string :matches "${match3}" "ik ben * met * en *." {
	fileinto "TEST 4: Hij is een ${1} met ${2} en ${3}!";
} else {
	fileinto "FAILED 4: ${match3}";
	stop;
}

# Test 5

set "match4" "beter van niet?";

if string :matches "${match4}" "*\\?" {
	fileinto "TEST 5: ${1}";
} else { 
	fileinto "FAILED 5: ${match4}";
	stop;
}

# Test 6


set "match5" "The quick brown fox jumps over the lazy dog.";

if string :matches "${match5}" "T?? ????? ????? ?o? ?u??? o?er ?he ???? ?o?." {
	fileinto "TEST 6: ${22}${8}${6}${25}${2}${13}${26}${1}${5}${15}${7}${21}${16}${12}${10}${17}${3}${9}${18}${20}${4}${19}${11}${14}${24}${23}";
} else {
	fileinto "FAILED 6: ${match5}";
	#stop;
}
if true {

# Test 7
if string :matches "${match5}" "T?? ????? ?w??? ?o? ?u??? o?er ?he ???? ?o?." {
	fileinto "FAILED 7: ${match5}";
	stop;
} else {
	fileinto "TEST 7: OK";
}
}

# Test 8

set "match6" "zero:one:zero|three;one;zero/five";

if string :matches "${match6}" "*one?zero?five" {
	fileinto "TEST 8: o${2}z${3}f   (${1})";
} else {
	fileinto "FAILED 8: ${match6}";
	stop;
}

# Test 9

set "match7" "frop";

if string :matches "${match7}" "??op" {
	fileinto "TEST 9: ${1}-${2}-op";
} else {
	fileinto "FAILED 9: ${match7}";
	#stop;
}

# Test 10

if string :matches "${match7}" "fr??" {
	fileinto "TEST 10: fr-${1}-${2}";
} else {
	fileinto "FAILED 10: ${match7}";
	#stop;
}

# Test 11

set "match8" "klopfropstroptop";

if string :matches "${match8}" "*fr??*top" {
	fileinto "TEST 11: ${1}: fr-${2}-${3}: ${4}";
} else {
	fileinto "FAILED 11: ${match8}";
	#stop;
}

if string :matches "${match8}" "?*fr??*top" {
	fileinto "TEST 12: ${1}-${2}: fr-${3}-${4}: ${5}";
} else {
	fileinto "FAILED 12: ${match8}";
	#stop;
}

if string :matches "${match8}" "*?op" {
	fileinto "TEST 13: ${1} ${2} op";
} else {
	fileinto "FAILED 13: ${match8}";
	#stop;
}

if string :matches "${match8}" "*?op*" {
	fileinto "TEST 14: (*?op*): ${1}:${2}:${3}:${4}:${5}:${6}:${7}:";
} else {
	fileinto "FAILED 14: ${match8}";
	#stop;
}
