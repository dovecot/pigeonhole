require "enotify";

# 1: Invalid header name 
notify "mailto:stephan@rename-it.nl?header:=frop";

# 2: Invalid recipient
notify "mailto:stephan%23rename-it.nl";

# 3: Invalid to header recipient
notify "mailto:stephan@rename-it.nl?to=nico%23vestingbar.nl";

