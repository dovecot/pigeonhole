require "include";
require "variables";
require "fileinto";

import ["value1", "value2"];
export ["result1"];

fileinto "${value1} ${value2}";

set "result1" "${value1} ${value2}";

fileinto "RESULT: ${result1}";
