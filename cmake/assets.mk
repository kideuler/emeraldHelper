# cmake/assets.mk
#
# Asset-generation driver invoked BY CMake (via `make -f cmake/assets.mk`,
# never invoked directly by a developer) to turn source assets (.png, .pal,
# .wav, .mid, .json) into the generated files the ROM sources #include or
# .incbin.
#
# Why this exists instead of being reimplemented as CMake custom commands:
# pokeemerald's own tooling (see tools/preproc's INCGFX_* handling and
# tools/scaninc's matching logic) generates GNU Make *rules* on the fly,
# with per-callsite gbagfx arguments baked in, and writes them into the
# per-source .d fragments produced by scaninc (see cmake/Assets.cmake).
# That is genuinely dynamic build-graph generation, keyed off of macro
# invocations inside the C sources themselves. Make is uniquely able to
# consume that output directly (it *is* Make syntax); reimplementing the
# INCGFX argument-encoding scheme in another language would duplicate
# logic that must stay byte-for-byte in sync across three places
# (preproc, scaninc, and the reimplementation) for no benefit. So this
# thin driver reuses the project's own graphics/map/json/audio rule files
# verbatim, wires their tool variables to the CMake-built executables, and
# is invoked as one CMake build step. Everything else (compiling, linking,
# both build variants, gbafix, compare) is plain CMake with no Make
# involved.

# --- Tool paths, supplied by CMake ---
GFX       ?= gbagfx
WAV2AGB   ?= wav2agb
MID       ?= mid2agb
PREPROC   ?= preproc
MAPJSON   ?= mapjson
JSONPROC  ?= jsonproc

# --- Layout, matches the main project ---
C_SUBDIR          ?= src
DATA_SRC_SUBDIR   ?= src/data
DATA_ASM_SUBDIR   ?= data
MID_SUBDIR        ?= sound/songs/midi
SONG_SUBDIR       ?= sound/songs
SONG_BUILDDIR     ?= build/dummy_song_dir
MID_BUILDDIR      ?= build/dummy_mid_dir

SHELL := bash

MAKEFLAGS += --no-print-directory
.SUFFIXES:
.SECONDARY:
.DELETE_ON_ERROR:

AUTO_GEN_TARGETS :=
include graphics_file_rules.mk
include map_data_rules.mk
include json_data_rules.mk

# audio_rules.mk defines song/cry/sound .o pattern rules that require a
# real object build directory; we only want its .mid -> .s and .wav -> .bin
# asset rules here, so its object-file rules are harmless no-ops (their
# targets are never requested by this driver).
include audio_rules.mk

# Generic asset pattern rules (verbatim from the top-level Makefile).
%.s:   ;
%.png: ;
%.pal: ;
%.wav: ;

%.1bpp:   %.png  ; $(GFX) $< $@
%.4bpp:   %.png  ; $(GFX) $< $@
%.8bpp:   %.png  ; $(GFX) $< $@
%.gbapal: %.pal  ; $(GFX) $< $@
%.gbapal: %.png  ; $(GFX) $< $@
%.lz:     %      ; $(GFX) $< $@
%.rl:     %      ; $(GFX) $< $@

# Vanilla pokeemerald only builds per-map header.inc/events.inc/
# connections.inc on demand, as explicit prerequisites of data/maps.o and
# data/map_events.o (see map_data_rules.mk) — scaninc can't discover that
# dependency itself, since maps.s .includes data/maps/headers.inc, which
# doesn't exist until map_data_rules.mk's grouped-target rule runs, so
# scaninc has nothing on disk yet to recurse into for the per-map files
# nested beneath it. Rather than hand data/maps.o and data/map_events.o's
# CMake custom_commands the full MAP_HEADERS/MAP_EVENTS/MAP_CONNECTIONS
# lists as extra explicit DEPENDS (duplicating map_data_rules.mk's own
# glob in CMake), it's simplest to fold them into `generated` and
# eagerly build every map's data every time; with ~500 maps this is cheap
# relative to the rest of the build.
.PHONY: generated
generated: $(AUTO_GEN_TARGETS) $(MAP_HEADERS) $(MAP_EVENTS) $(MAP_CONNECTIONS)
	@:

# Every song's .mid is unconditionally compiled into the ROM (it's not
# discovered via .incbin/INCGFX scanning like graphics/cries are), so its
# intermediate .s must always be generated, not just when referenced.
MID_MIDS := $(wildcard $(MID_SUBDIR)/*.mid)
MID_ASM_OUTPUTS := $(patsubst $(MID_SUBDIR)/%.mid,$(MID_SUBDIR)/%.s,$(MID_MIDS))

.PHONY: midi_asm
midi_asm: $(MID_ASM_OUTPUTS)
	@:

# INCGFX-driven rules discovered by scaninc, plus each source's plain
# .incbin dependency list. See cmake/Assets.cmake for how these are
# produced.
-include $(shell find $(DEPSCAN_DIR) -name '*.d')

# The concrete list of generated asset paths actually referenced by the
# sources about to be compiled: cmake/collect_asset_goals.py writes them
# as `ASSET_GOALS := ...` into $(DEPSCAN_DIR)/goals.mk.
ASSET_GOALS :=
-include $(DEPSCAN_DIR)/goals.mk

.PHONY: assets
assets: generated midi_asm $(ASSET_GOALS)
	@:
