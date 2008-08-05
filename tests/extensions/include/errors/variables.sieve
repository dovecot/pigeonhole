require "include";
require "variables";

# Importing unknown variable, but not a compile-time error
import "frop";

# Importing unknown variable, but not a compile-time error
import ["friep", "frml"];

# Cannot export imported variable
export ["friep"];

# Import after export
import "friep";

keep;

# Export after command not being require, import or export
export "friep";

