require "editheader";

/* "addheader" [":last"] <field-name: string> <value: string>
 */

# 1: missing field name and value
addheader;

# 2: missing value
addheader "x-frop";

# 3: value not a string; number
addheader "x-frop" 2;

# 4: value not a string; list
addheader "x-frop" ["frop"];

# 5: strange tag
addheader :tag "x-frop" "frop";

/* "deleteheader" [":index" <fieldno: number> [":last"]]
 *                  [COMPARATOR] [MATCH-TYPE]
 *                  <field-name: string>
 *                  [<value-patterns: string-list>]
 */

# 6: missing field name
deleteheader;

# 7: :last tag without index
deleteheader :last "x-frop";

# 8: :index tag with string argument
deleteheader :index "frop" "x-frop";

# OK: match type without value patterns
deleteheader :matches "x-frop";

# 9: value patterns not a string(list)
deleteheader "x-frop" 1;


