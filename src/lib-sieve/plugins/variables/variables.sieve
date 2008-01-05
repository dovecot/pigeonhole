require "variables";
require "fileinto";

set :upper "foo" "foosome";
set :lower "bar" "bareable";

fileinto "${foo}.${bar}";
