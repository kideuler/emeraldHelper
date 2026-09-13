#!/usr/bin/env bash
set -euo pipefail

# cmake/build_agbcc.sh
#
# Thin wrapper around the agbcc submodule's own build.sh + install.sh
# (see agbcc/.github/workflows/build.yml -- CI-verified on Linux and
# macOS with the same brew-installed arm-none-eabi-* toolchain this repo
# already requires). Deliberately not reimplemented as native CMake
# rules: agbcc's build is a recursive make+autoconf pipeline with a real
# ordering dependency (old_agbcc must exist before libgcc/libc build,
# and the gcc/ objects must be `make clean`ed between the old_agbcc and
# agbcc passes since their CFLAGS differ) that's easy to get subtly
# wrong by hand-translating, and this reuses its own from-scratch build
# exactly as INSTALL.md's manual steps do.
#
# Usage: build_agbcc.sh <agbcc_src_dir> <dest_root>
#   <agbcc_src_dir>  path to the agbcc submodule checkout
#   <dest_root>      repo root install.sh installs into
#                     (creates <dest_root>/tools/agbcc/{bin,include,lib})

agbcc_src="$1"
dest_root="$2"

cd "$agbcc_src"
sh build.sh
sh install.sh "$dest_root"
