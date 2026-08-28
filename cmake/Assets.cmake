# Drives generation of every asset (converted graphics, compressed audio,
# generated map/json headers, MIDI-derived song assembly) that the ROM
# sources need, before those sources are compiled. See cmake/assets.mk for
# why this reuses the project's original *_rules.mk files under the hood.
#
# Expects: C_SRCS, C_ASM_SRCS, ASM_SRCS, DATA_ASM_SRCS (lists of source
# files, already collected by the top-level CMakeLists.txt), and the
# scaninc/gbagfx/wav2agb/mid2agb/preproc/mapjson/jsonproc targets.

set(DEPSCAN_DIR ${CMAKE_SOURCE_DIR}/build/depscan)

set(_depscan_outputs)

# second_include is the literal value passed after a second "-I" flag,
# possibly empty (mirrors the original Makefile's `-I ""` for asm sources,
# which matters: it is not the same as omitting the flag, see scaninc's
# argv handling of a bare "-I").
function(_pokeemerald_add_depscan src second_include)
  file(RELATIVE_PATH relsrc ${CMAKE_SOURCE_DIR} ${src})
  set(out ${DEPSCAN_DIR}/${relsrc}.d)
  get_filename_component(outdir ${out} DIRECTORY)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${outdir}
    COMMAND $<TARGET_FILE:scaninc> -M ${out} -g ${CMAKE_SOURCE_DIR}/build/assets
            -I ${CMAKE_SOURCE_DIR}/include -I "${second_include}" ${src}
    DEPENDS ${src} scaninc
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Scanning ${relsrc}"
    VERBATIM
  )
  set(_depscan_outputs ${_depscan_outputs} ${out} PARENT_SCOPE)
endfunction()

foreach(src ${C_SRCS})
  _pokeemerald_add_depscan(${src} "${CMAKE_SOURCE_DIR}/tools/agbcc/include")
endforeach()
foreach(src ${C_ASM_SRCS} ${ASM_SRCS} ${DATA_ASM_SRCS})
  _pokeemerald_add_depscan(${src} "")
endforeach()

add_custom_target(depscan_all DEPENDS ${_depscan_outputs})

set(ASSET_GOALS_MK ${DEPSCAN_DIR}/goals.mk)
add_custom_command(
  OUTPUT ${ASSET_GOALS_MK}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/collect_asset_goals.py
          ${DEPSCAN_DIR} ${ASSET_GOALS_MK}
  DEPENDS depscan_all ${CMAKE_SOURCE_DIR}/cmake/collect_asset_goals.py
  COMMENT "Collecting required generated-asset list"
  VERBATIM
)
add_custom_target(collect_asset_goals DEPENDS ${ASSET_GOALS_MK})

set(_assets_stamp ${CMAKE_SOURCE_DIR}/build/.assets_built)
add_custom_command(
  OUTPUT ${_assets_stamp}
  COMMAND ${MAKE_EXECUTABLE} --no-print-directory -f ${CMAKE_SOURCE_DIR}/cmake/assets.mk assets
          GFX=$<TARGET_FILE:gbagfx>
          WAV2AGB=$<TARGET_FILE:wav2agb>
          MID=$<TARGET_FILE:mid2agb>
          PREPROC=$<TARGET_FILE:preproc>
          MAPJSON=$<TARGET_FILE:mapjson>
          JSONPROC=$<TARGET_FILE:jsonproc>
          DEPSCAN_DIR=${DEPSCAN_DIR}
  COMMAND ${CMAKE_COMMAND} -E touch ${_assets_stamp}
  DEPENDS collect_asset_goals gbagfx wav2agb mid2agb preproc mapjson jsonproc
          ${CMAKE_SOURCE_DIR}/cmake/assets.mk
          ${CMAKE_SOURCE_DIR}/graphics_file_rules.mk
          ${CMAKE_SOURCE_DIR}/map_data_rules.mk
          ${CMAKE_SOURCE_DIR}/json_data_rules.mk
          ${CMAKE_SOURCE_DIR}/audio_rules.mk
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Generating graphics/audio/map/json assets"
  VERBATIM
)
add_custom_target(generate_assets DEPENDS ${_assets_stamp})
