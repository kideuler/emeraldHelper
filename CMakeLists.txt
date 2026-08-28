cmake_minimum_required(VERSION 3.20)

# This is a parallel, opt-in build system alongside the project's original
# Makefile (see Makefile's header and INSTALL.md), not a replacement.
# `make`/`make modern`/`make compare` keep working exactly as before.
# Use this when you'd rather drive the build (or an IDE) through CMake.
#
# Two independent build variants exist, matching the Makefile's MODERN
# flag, selected via -DMODERN=ON/OFF at configure time (a variant switch,
# not a regular CMake option a user toggles post-configure, since it
# changes which compiler and which linker script are used):
#   -DMODERN=OFF (default) - "matching" build using the external agbcc
#                 compiler (see INSTALL.md), byte-identical to retail when
#                 the `compare` target is run against rom.sha1.
#   -DMODERN=ON  - modern arm-none-eabi-gcc build, no agbcc dependency,
#                 not byte-identical, no `compare` target.
#
# Known gap vs. the Makefile: this build does not re-scan header
# dependencies on every build the way `scaninc`-generated .d files +
# NODEP=0 do in the Makefile. Editing a source or asset file's own path
# triggers a rebuild; editing a transitively #included header does not
# automatically trigger recompilation of its includers under CMake. Run a
# clean build (or `touch` affected sources) after broad header changes,
# or use the Makefile for that workflow.
project(pokeemerald NONE)

option(MODERN "Build with modern arm-none-eabi-gcc instead of agbcc" OFF)
option(DINFO "Include debug info in the build" OFF)
set(O_LEVEL "2" CACHE STRING "Optimization level passed to the GBA compiler")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

find_program(MAKE_EXECUTABLE NAMES make gmake REQUIRED)
find_program(PERL_EXECUTABLE perl REQUIRED)
find_program(SHA1SUM_EXECUTABLE sha1sum)
find_program(SHASUM_EXECUTABLE shasum)
if(SHA1SUM_EXECUTABLE)
  set(SHA1_EXECUTABLE ${SHA1SUM_EXECUTABLE} -c)
elseif(SHASUM_EXECUTABLE)
  set(SHA1_EXECUTABLE ${SHASUM_EXECUTABLE} -c)
else()
  message(FATAL_ERROR "Need sha1sum or shasum for the `compare` target")
endif()

find_program(CROSS_AS  arm-none-eabi-as  REQUIRED)
find_program(CROSS_LD  arm-none-eabi-ld  REQUIRED)
find_program(CROSS_AR  arm-none-eabi-ar  REQUIRED)
find_program(CROSS_OBJCOPY arm-none-eabi-objcopy REQUIRED)
find_program(CROSS_OBJDUMP arm-none-eabi-objdump REQUIRED)
# Host tools are a mix of C and C++; this also covers CMAKE_C_COMPILER,
# only actually invoked when MODERN=ON, letting a missing cross toolchain
# fail fast at configure time either way.
enable_language(C)
enable_language(CXX)

# --- Host-side tools (always built with the host's native compiler) ---
add_subdirectory(tools)

# --- libagbsyscall.a (cross-assembled BIOS syscall wrapper archive) ---
add_subdirectory(libagbsyscall)

# --- Collect ROM sources (mirrors the Makefile's $(wildcard ...) collection) ---
file(GLOB C_SRCS
  ${CMAKE_SOURCE_DIR}/src/*.c
  ${CMAKE_SOURCE_DIR}/src/*/*.c
  ${CMAKE_SOURCE_DIR}/src/*/*/*.c
)
list(FILTER C_SRCS EXCLUDE REGEX "\\.inc\\.c$")

file(GLOB C_ASM_SRCS
  ${CMAKE_SOURCE_DIR}/src/*.s
  ${CMAKE_SOURCE_DIR}/src/*/*.s
  ${CMAKE_SOURCE_DIR}/src/*/*/*.s
)

file(GLOB ASM_SRCS ${CMAKE_SOURCE_DIR}/asm/*.s)

file(GLOB DATA_ASM_SRCS ${CMAKE_SOURCE_DIR}/data/*.s)

file(GLOB MID_SRCS ${CMAKE_SOURCE_DIR}/sound/songs/midi/*.mid)

# --- Asset generation (graphics/audio/map/json), then the ROM itself ---
include(cmake/Assets.cmake)
include(cmake/Rom.cmake)
