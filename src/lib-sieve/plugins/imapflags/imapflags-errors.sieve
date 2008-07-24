require "imap4flags";
require "fileinto";

setflag;
addflag 2;
setflag "flagvar" 2;
addflag 2 "$MDNRequired";
removeflag ["flagvar"] "$MDNRequired";

hasflag;

if hasflag 3 {
	removeflag "$MDNRequired";
}

if hasflag "flagvar" ["$MDNRequired", "\\Seen"] {
    removeflag "$MDNRequired";
}

removeflag "\\frop";
