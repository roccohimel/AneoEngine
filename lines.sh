#!/bin/bash

#AneoEngine repo source line counter
#the fewer lines, the more beautiful! -Rocco Himels

echo "Counting lines per file:"
git ls-files | grep '\.c' | xargs wc -l
git ls-files | grep '\.AC' | xargs wc -l
git ls-files | grep '\.ASM' | xargs wc -l
git ls-files | grep '\.sh' | xargs wc -l
git ls-files | grep '\.AC.S' | xargs wc -l
