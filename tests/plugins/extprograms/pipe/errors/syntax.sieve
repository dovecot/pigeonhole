require "vnd.dovecot.pipe";

# 1: error: no arguments
pipe;

# 2: error: numeric argument
pipe 1;

# 3: error: tag argument
pipe :frop;

# 4: error: numeric second argument
pipe "sdfd" 1;

# 5: error: stringlist first argument
pipe ["sdfd","werwe"] "sdfs";

# 6: error: too many arguments
pipe "sdfd" "werwe" "sdfs";

# 7: error: inappropriate :copy argument
pipe :try :copy "234234" ["324234", "23423"];
