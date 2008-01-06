require "variables";
require "fileinto";

set :upper "foo" "foosome";
set :lower "bar" "bareable";

#fileinto "${foo}.${bar}";

#set "huts" "${foo} likes the ${bar}";
#set "fluts" "${foo}";
set "friep" "it is ${foo} but not ${bar}!";

fileinto "${friep}";
