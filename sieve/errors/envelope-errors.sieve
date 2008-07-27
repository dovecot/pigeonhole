require "envelope";

if envelope :is "to" "frop@rename-it.nl" {
}

if envelope :is "frop" "frop@rename-it.nl" {
}

if envelope :is ["to","from"] "frop@rename-it.nl" {
}

if envelope :is ["to","frop"] "frop@rename-it.nl" {
}
