require "include";
require "variables";

global "result";

set "result" "${result} TWO";

keep;

include "once-1.sieve";

return;
