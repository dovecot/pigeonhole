require "include";
require "variables";
require "fileinto";

import "result1";
import "result2";
export "result";

set "result" "${result1} ${result2}";

fileinto "RESULT: ${result}";
