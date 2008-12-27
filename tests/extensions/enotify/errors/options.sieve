require "enotify";

# 1: empty option
notify :options "" "mailto:stephan@rename-it.nl";

# 2: invalid option name syntax
notify :options "frop" "mailto:stephan@rename-it.nl";

# 3: invalid option name syntax
notify :options "_frop=" "mailto:stephan@rename-it.nl";

# 4: invalid option name syntax
notify :options "=frop" "mailto:stephan@rename-it.nl";

# 5: invalid value
notify :options "frop=frml
frop" "mailto:stephan@rename-it.nl";

