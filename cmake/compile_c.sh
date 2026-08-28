#!/usr/bin/env bash
# Reproduces the C compile pipeline from the top-level Makefile:
#   cpp | preproc | cc1 | (append a closing .text alignment) | as
# as a script because CMake custom commands can't easily express a shell
# pipeline with process substitution portably.
set -euo pipefail

cpp=$1
cppflags=$2
preproc=$3
assets_dir=$4
charmap=$5
cc1=$6
cflags=$7
as=$8
asflags=$9
shift 9
src=$1
out=$2

# shellcheck disable=SC2086
$cpp $cppflags "$src" \
  | "$preproc" -i -g "$assets_dir" "$src" "$charmap" \
  | $cc1 $cflags -o - - \
  | cat - <(printf '.text\n\t.align\t2, 0\n') \
  | $as $asflags -o "$out" -
