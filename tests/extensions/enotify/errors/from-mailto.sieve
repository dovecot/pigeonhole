require "enotify";

# 1: Invalid from address
notify :from "stephan#rename-it.nl" "mailto:stephan@example.com";

# 2: Empty from address
notify :from "" "mailto:stephan@example.com";
