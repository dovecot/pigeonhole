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
	stop;
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
