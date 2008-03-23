require "variables";
require "regex";
require "fileinto";

set "match1" "Hutseflutsfropfrml";

if string :regex "${match1}" "Hutse(.+)fropfrml" {
	fileinto "${0}:${1}:${2}";
}

if string :regex "${match1}" "(.+)f((.+)f(.+))f(.+)" {
	fileinto "${0}:${1}:${2}:${3}:${4}:${5}";
}

if string :regex "${match1}" "(.+)((g(.+)friep)|(f(.+)frop))(.+)" {
	fileinto "${0}:${1}:${2}:${3}:${4}:${5}:${6}:${7}";
}

