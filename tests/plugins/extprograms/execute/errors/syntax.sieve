require "vnd.dovecot.execute";

# 1: error: no arguments
execute;

# 2: error: numeric argument
execute 1;

# 3: error: tag argument
execute :frop;

# 4: error: numeric second argument
execute "sdfd" 1;

# 5: error: stringlist first argument
execute ["sdfd","werwe"] "sdfs";

# 6: error: too many arguments
execute "sdfs" "sdfd" "werwe";

# 7: error: inappropriate :copy argument
execute :copy "234234" ["324234", "23423"];

# 8: error: invalid :input argument; missing parameter
execute :input "frop";

# 9: error: invalid :input argument; invalid parameter
execute :input 1 "frop";

# 10: error: invalid :input argument; invalid parameter
execute :input ["23423","21342"] "frop";

# 11: error: invalid :input argument; invalid parameter
execute :input :frop "frop";

# 12: error: :output not allowed without variables extension
execute :output "${frop}" "frop";

