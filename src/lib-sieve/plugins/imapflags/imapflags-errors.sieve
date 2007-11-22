require "imapflags";
require "fileinto";

setflag;
addflag 2;
setflag "flagvar" 2;
addflag 2 "$MDNRequired";
removeflag ["flagvar"] "$MDNRequired";
