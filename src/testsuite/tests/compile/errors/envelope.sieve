/*
 * Envelope test errors
 *
 * Total errors: 2 (+1 = 3)
 */

require "envelope";

# Not an error 
if envelope :is "to" "frop@rename-it.nl" {
}

# Unknown envelope part (1)
if envelope :is "frop" "frop@rename-it.nl" {
}

# Not an error
if envelope :is ["to","from"] "frop@rename-it.nl" {
}

# Unknown envelope part (2)
if envelope :is ["to","frop"] "frop@rename-it.nl" {
}
