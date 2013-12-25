# Test various white space occurrences
redirect "stephan@example.org";
redirect " stephan@example.org";
redirect "stephan @example.org";
redirect "stephan@ example.org";
redirect "stephan@example.org ";
redirect " stephan @ example.org ";
redirect "Stephan Bosch<stephan@example.org>";
redirect " Stephan Bosch<stephan@example.org>";
redirect "Stephan Bosch <stephan@example.org>";
redirect "Stephan Bosch< stephan@example.org>";
redirect "Stephan Bosch<stephan @example.org>";
redirect "Stephan Bosch<stephan@ example.org>";
redirect "Stephan Bosch<stephan@example.org >";
redirect "Stephan Bosch<stephan@example.org> ";
redirect "  Stephan Bosch  <  stephan  @  example.org  > ";

# Test address syntax
redirect "\"Stephan Bosch\"@example.org";
redirect "Stephan.Bosch@eXamPle.oRg";
redirect "Stephan.Bosch@example.org";
redirect "Stephan Bosch <stephan@example.org>";

