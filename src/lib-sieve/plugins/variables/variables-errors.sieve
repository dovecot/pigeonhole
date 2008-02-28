require "variables";
require "fileinto";

set :upper :lower "frop" "FrOp";
set :lowerfirst :upperfirst "friep" "Friep";

fileinto "${frop}";
fileinto "${friep}";

set "0" "Frop";
set "frop." "Friep";
set "frop.friep" "Frml";
