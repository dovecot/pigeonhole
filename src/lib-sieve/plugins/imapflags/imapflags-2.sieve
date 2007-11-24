require "imap4flags";
require "fileinto";

setflag "FRop FRML Hutsefluts Friep Slorf";
addflag ["derf", "slorf", "werty", "drolf"];
addflag ["frop", "frml", "frep"];

removeflag ["frop", "slorf"];
removeflag "hutsefluts frep";
addflag "fiiierp";

if hasflag :is "Werty" {
	fileinto "FROP";
}

if hasflag :is "eek" {
	fileinto "EEK";
}
