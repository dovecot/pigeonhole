require "vnd.dovecot.filter";

# 1: error: no arguments
filter;

# 2: error: numeric argument
filter 1;

# 3: error: tag argument
filter :frop;

# 4: error: numeric second argument
filter "sdfd" 1;

# 5: error: stringlist first argument
filter ["sdfd","werwe"] "sdfs";

# 6: error: too many arguments
filter "sdfd" "werwe" "sdfs";

# 7: error: inappropriate :copy argument
filter :try :copy "234234" ["324234", "23423"];
