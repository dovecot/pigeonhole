require ["vacation", "include", "fileinto"];

vacation "Ik ben er ff niet.";
include "included1";
keep;
stop;
fileinto "Not supposed to happen.";

