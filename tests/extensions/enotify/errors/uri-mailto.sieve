require "enotify";

# 1: Invalid character in to part
notify "mailto:stephan@rename-it.nl;?header=frop";

# 2: Invalid character in hname
notify "mailto:stephan@rename-it.nl?header<=frop";

# 3: Invalid character in hvalue
notify "mailto:stephan@rename-it.nl?header=fr>op";

# 4: Invalid header name 
notify "mailto:stephan@rename-it.nl?header:=frop";

# 5: Invalid recipient
notify "mailto:stephan%23rename-it.nl";

# 6: Invalid to header recipient
notify "mailto:stephan@rename-it.nl?to=nico%23vestingbar.nl";

