require "notify";

# 1: empty option
notify :options "";

# 2: invalid address syntax
notify :options "frop#vestingbar.nl";

# Valid
notify :options "frop@vestingbar.nl";

