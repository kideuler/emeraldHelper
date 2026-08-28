#!/usr/bin/env bash
# Reproduces the Makefile's pipeline for assembly sources that still go
# through the text preprocessor twice (src/*.s and data/*.s):
#   preproc | cpp | preproc -ie | as
set -euo pipefail

preproc=$1
cpp=$2
include_dir=$3
charmap=$4
as=$5
asflags=$6
src=$7
out=$8

# shellcheck disable=SC2086
"$preproc" "$src" "$charmap" \
  | $cpp -I "$include_dir" - \
  | "$preproc" -ie "$src" "$charmap" \
  | $as $asflags -o "$out"
