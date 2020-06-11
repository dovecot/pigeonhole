require "fileinto";
require "encoded-character";

/*
 * Fileinto errors
 *
 * Total erors: 9 (+1 = 10)
 */

# Missing string argument
fileinto;

# Spurious test
fileinto true;

# Spurious test
fileinto "Frop" true;

# Spurious number argument
fileinto 33;

# Spurious tag argument
fileinto :frop;

# Spurious additional string argument
fileinto "Frop" "Friep";

# Spurious additional number argument
fileinto "Frop" 123;

# Spurious additional tag argument
fileinto "Frop" :frop;

# Bad mailbox name
fileinto "${hex:ff}rop";

# Not an error
fileinto "Frop";
