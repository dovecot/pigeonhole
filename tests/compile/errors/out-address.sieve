require "vacation";

# Error 

redirect "@wrong.example.com";
redirect "error";
redirect "error@";
redirect "Stephan Bosch error@rename-it.nl";
redirect "Stephan Bosch <error@rename-it.nl";
redirect " more error @  example.com  ";
redirect "@";
redirect "<>";
redirect "Error <";
redirect "Error <stephan";
redirect "Error <stephan@";
redirect "stephan@rename-it.nl,tss@iki.fi";
redirect "stephan@rename-it.nl,%&^&!!~";

vacation :from "Error" "Ik ben er niet.";

# Ok

redirect "Ok Good <stephan@rename-it.nl>";
redirect "ok@example.com";
redirect " more  @  example.com  ";

vacation :from "good@voorbeeld.nl" "Ik ben weg!";
