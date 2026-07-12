/*
 * Encoded-character errors
 *
 * Total errors: 4 (+1 = 5)
 */

require "encoded-character";
require "fileinto";

# Invalid unicode character (1)
fileinto "INBOX.${unicode:200000}";

# Not an error
fileinto "INBOX.${unicode:200000";

# Invalid unicode character (2)
fileinto "INBOX.${Unicode:DF01}";

# Not an error
fileinto "INBOX.${Unicode:DF01";

# Invalid unicode character: overflowing hex value (3)
fileinto "INBOX.${unicode:333333333}";

# Invalid unicode character: hex value overflowing even a 64-bit
# accumulator (4)
fileinto "INBOX.${unicode:33333333333333333}";



