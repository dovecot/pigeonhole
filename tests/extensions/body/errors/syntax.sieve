require "body";

# 1: No key list
if body { }

# 2: Number 
if body 3 { }

# OK: String
if body "frop" { }

# 3: To many arguments
if body "frop" "friep" { }

# 4: Unknown tag
if body :frop { }

# 5: Unknown tag with valid key
if body :friep "frop" { }

# 6: Content without argument
if body :content { }

# 7: Content without key argument
if body :content "frop" { }

# 8: Content with number argument
if body :content 3 "frop" { }

# 9: Content with unknown tag
if body :content :frml "frop" { }

# 10: Content with known tag
if body :content :contains "frop" {  }

# 11: Duplicate transform
if body :content "frop" :raw "frop" {  }

