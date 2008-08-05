/*
 * Import runtime test
 *
 * Tests whether the import directive fails when importing variables that were
 * never exported by a parent script or one of its sibblings. 
 */
require "include";
require "variables";

# This fails at runtime
import "global";

export "local";

keep;
