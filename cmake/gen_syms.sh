#!/usr/bin/env bash
# Reproduces the top-level Makefile's `$(SYM)` rule: a filtered, reordered
# dump of EWRAM/IWRAM/ROM symbols from the linked ELF.
set -euo pipefail

objdump=$1
perl=$2
elf=$3
out=$4

"$objdump" -t "$elf" \
  | sort -u \
  | grep -E '^0[2389]' \
  | "$perl" -p -e 's/^(\w{8}) (\w).{6} \S+\t(\w{8}) (\S+)$/\1 \2 \3 \4/g' \
  > "$out"
