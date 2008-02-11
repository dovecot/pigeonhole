require "variables";
require "fileinto";

set :length :upper "foo" "foosome";
set :quotewildcard :lower "bar" "bareable";

#fileinto "${foo}.${bar}";

#set "huts" "${foo} likes the ${bar}";
#set "fluts" "${foo}";
set "friep" "it is ${foo} but not ${bar}!";

fileinto "${friep}";
