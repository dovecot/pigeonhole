require "variables";
require "fileinto";

set :lower :upperfirst "foo" "FOOSOME";
set :upperfirst :lower "bar" "BARABLE";
set :upper :lowerfirst "hutse" "hutsefluts";
set :lowerfirst :upper "piep" "piepsnot";


#fileinto "${foo}.${bar}";

#set "huts" "${foo} likes the ${bar}";
#set "fluts" "${foo}";
set "friep" "it is ${foo} but not ${bar}!";
set "frop" "van je ${hutse} en ${piep}!";

fileinto "${friep}";
fileinto "${frop}";
