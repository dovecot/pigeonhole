require "fileinto";
require "comparator-i;ascii-numeric";

if header :is :comparator "i;ascii-numeric" "X-Spam-Score" "3T" {
	discard;
	stop;
}

keep;
