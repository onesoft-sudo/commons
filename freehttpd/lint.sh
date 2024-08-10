#! /bin/sh

sources=$(find . -type f -regex ".*/.*\.c" -regextype sed)
clang-tidy-18 $sources
exit $?
