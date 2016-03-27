require "vnd.dovecot.report";

# 1: Too few arguments
report;

# 2: Too few arguments
report "abuse";

# 3: Too few arguments
report "abuse" "Message is spam.";

# Not an error
report "abuse" "Message is spam." "frop@example.com";

# 4: Bad arguments
report "abuse" "Message is spam." 1;

# 5: Bad tag
report :frop "abuse" "Message is spam." "frop@example.com";

# 6: Bad sub-test
report "abuse" "Message is spam." "frop@example.com" frop;

# 7: Bad block
report "abuse" "Message is spam." "frop@example.com" { }

# 8: Bad feedback type
report "?????" "Message is spam." "frop@example.com";
