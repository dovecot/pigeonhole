# Test various white space occurences
redirect "stephan@rename-it.nl";
redirect " stephan@rename-it.nl";
redirect "stephan @rename-it.nl";
redirect "stephan@ rename-it.nl";
redirect "stephan@rename-it.nl ";
redirect " stephan @ rename-it.nl ";
redirect "Stephan Bosch<stephan@rename-it.nl>";
redirect " Stephan Bosch<stephan@rename-it.nl>";
redirect "Stephan Bosch <stephan@rename-it.nl>";
redirect "Stephan Bosch< stephan@rename-it.nl>";
redirect "Stephan Bosch<stephan @rename-it.nl>";
redirect "Stephan Bosch<stephan@ rename-it.nl>";
redirect "Stephan Bosch<stephan@rename-it.nl >";
redirect "Stephan Bosch<stephan@rename-it.nl> ";
redirect "  Stephan Bosch  <  stephan  @  rename-it.nl  > ";

# Test address syntax
redirect "\"Stephan Bosch\"@rename-it.nl";
redirect "Stephan.Bosch@rename-it.nl";
redirect "Stephan.Bosch@ReNaMe-It.Nl";
redirect "Stephan Bosch <stephan@rename-it.nl>";

