require "variables";
require "fileinto";

set "match1" "Test of general stupidity";

# Test 1
if string :matches "${match1}" "Test of *" {
	fileinto "TEST 1: ${1}";
}

# Test 2
if string :matches "${match1}" "of *" {
	fileinto "FAILED 2: ${1}";
} else {
	fileinto "TEST 2 NOT MATCHED: ${match1}";
}

set "match2" "toptoptop";

# Test 3
if string :matches "${match2}" "*top" {
	fileinto "TEST 3: ${1}";
} 

set "match3" "ik ben een tukker met grote oren en een lelijke broek.";

# Test 4
if string :matches "${match3}" "ik ben * met * en *." {
	fileinto "TEST 4: Hij is een ${1} met ${2} en ${3}!";
}
