require "vacation";

redirect "@example.com";
redirect "test";
redirect "test@";
redirect "Stephan Bosch stephan@rename-it.nl";
redirect "Stephan Bosch <stephan@rename-it.nl";

vacation :from "Test" "Ik ben er niet.";

# Ok
redirect "Stephan Bosch <stephan@rename-it.nl>";
redirect "hufter@example.com";
vacation :from "tukker@voorbeeld.nl" "Ik ben weg!";
