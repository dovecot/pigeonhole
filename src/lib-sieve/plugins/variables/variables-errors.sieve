require "variables";
require "fileinto";

set :upper :lower "frop" "FrOp";
set :lowerfirst :upperfirst "friep" "Friep";

fileinto "${frop}";
fileinto "${friep}";
