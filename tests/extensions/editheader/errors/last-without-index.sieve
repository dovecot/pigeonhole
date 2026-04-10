require "editheader";

# :last without :index: must fail validation before field name checks
deleteheader :last "X-field:";
