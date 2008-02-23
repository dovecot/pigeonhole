require "variables";
require "fileinto";

set :lower :upperfirst "foo" "FOOSOME";
set :upperfirst :lower "bar" "BARABLE";
set :upper :lowerfirst "HutsE" "hutsefluts";
set :lowerfirst :upper "pIEp" "piepsnot";

#fileinto "${foo}.${bar}";

#set "huts" "${foo} likes the ${bar}";
#set "fluts" "${foo}";
set "friep" "it is ${foo} but not ${bar}!";
set "frop" "van je ${hutse} en ${piep}!";

set :length "len_frop" "${frop}";
set :quotewildcard "quote_friep" "frop*friep\\frml?";

fileinto "${friep}";
fileinto "${frop}";
fileinto "LEN-${len_frop}";
fileinto "${quote_friep}";

if string "${foo}" "foosome" {
	keep;
} elsif string :comparator "i;ascii-casemap" "${foo}" "foosome" {
	fileinto "CASE";
}
