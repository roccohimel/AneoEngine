#!/bin/bash
echo "[*] Counting lines per file... "
git ls-files | grep '\.c' | xargs wc -l
git ls-files | grep '\.AC' | xargs wc -l
git ls-files | grep '\.ASM' | xargs wc -l
git ls-files | grep '\.sh' | xargs wc -l
