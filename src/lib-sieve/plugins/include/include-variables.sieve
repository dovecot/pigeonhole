require "include";
require "variables";
require "fileinto";

export ["value1", "value2"];
export ["value3", "value4"];
export ["result1", "result2"];
export "result";

set "value1" "Works";
set "value2" "fine.";
set "value3" "Yeah";
set "value4" "it does.";

include "include-variables1";
include "include-variables2";
include "include-variables3";

fileinto "${result}";
