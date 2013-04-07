require "variables";
require "encoded-character";
require "vnd.dovecot.pipe";

# Slash
pipe "../frop";

# More slashes
pipe "../../james/sieve/vacation";

# 0000-001F; [CONTROL CHARACTERS]
pipe "idiotic${unicode: 001a}";

# 007F; DELETE
pipe "idiotic${unicode: 007f}";

# 0080-009F; [CONTROL CHARACTERS]
pipe "idiotic${unicode: 0085}";

# 2028; LINE SEPARATOR
pipe "idiotic${unicode: 2028}";

# 2029; PARAGRAPH SEPARATOR
pipe "idiotic${unicode: 2029}";

