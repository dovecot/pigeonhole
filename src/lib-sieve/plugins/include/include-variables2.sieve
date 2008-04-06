require "include";
require "variables";
require "fileinto";

import "value3";
import "value4";
export "result2";

fileinto "${value3} ${value4}";

set "result2" "${value3} ${value4}";

fileinto "RESULT: ${result2}";
