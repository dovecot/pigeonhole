require "editheader";

# Ok
addheader "X-field" "Frop";

# Invalid ':'
addheader "X-field:" "Frop";

# Invalid ' '
addheader "X field" "Frop";

# Ok
deleteheader "X-field";

# Invalid ':'
deleteheader "X-field:";

# Invalid ' '
deleteheader "X field";
