# Compiles and links the GBA ROM itself: the C/asm compile pipeline,
# generated linker symbol scripts, the final link + gbafix + objcopy
# steps, and (for the matching/agbcc build) the `compare` verification
# target. Everything here is a direct translation of the top-level
# Makefile's rules (see the header comment there for the historical
# reasoning behind the odd two-pass preprocess/compile/assemble pipeline).
#
# Expects C_SRCS, C_ASM_SRCS, ASM_SRCS, DATA_ASM_SRCS, MID_SRCS to already
# be set (see top-level CMakeLists.txt), plus the cross-toolchain and host
# tool variables it resolves itself below.

set(TITLE "POKEMON EMER")
set(GAME_CODE "BPEE")
set(MAKER_CODE "01")
set(REVISION "0")

if(MODERN)
  set(ROM_NAME pokeemerald_modern.gba)
  set(OBJ_DIR ${CMAKE_SOURCE_DIR}/build/modern)
  set(LD_SCRIPT ${CMAKE_SOURCE_DIR}/ld_script_modern.ld)
else()
  set(ROM_NAME pokeemerald.gba)
  set(OBJ_DIR ${CMAKE_SOURCE_DIR}/build/emerald)
  set(LD_SCRIPT ${CMAKE_SOURCE_DIR}/ld_script.ld)
endif()

set(ROM ${CMAKE_SOURCE_DIR}/${ROM_NAME})
string(REGEX REPLACE "\\.gba$" ".elf" ELF ${ROM})
string(REGEX REPLACE "\\.gba$" ".map" MAP ${ROM})
string(REGEX REPLACE "\\.gba$" ".sym" SYM ${ROM})

set(C_BUILDDIR ${OBJ_DIR}/src)
set(ASM_BUILDDIR ${OBJ_DIR}/asm)
set(DATA_ASM_BUILDDIR ${OBJ_DIR}/data)
set(MID_BUILDDIR ${OBJ_DIR}/sound/songs/midi)

set(ASSETS_DIR ${CMAKE_SOURCE_DIR}/build/assets)
set(CHARMAP ${CMAKE_SOURCE_DIR}/charmap.txt)

if(MODERN)
  set(MODERN_INT 1)
else()
  set(MODERN_INT 0)
endif()
set(ASFLAGS -mcpu=arm7tdmi --defsym MODERN=${MODERN_INT})

# Object/link steps that invoke the agbcc compiler binaries or link
# against its libgcc.a/libc.a need to depend on agbcc_toolchain (from
# cmake/Agbcc.cmake) so `cmake --build` builds/installs agbcc first
# instead of failing on a missing binary. Empty (no dependency) under
# MODERN=ON, where that target doesn't exist.
if(NOT MODERN)
  set(AGBCC_DEP agbcc_toolchain)
else()
  set(AGBCC_DEP)
endif()

# --- Per-variant CPP / CC1 / CFLAGS / LIB selection ---
if(NOT MODERN)
  # AGBCC_DIR is set by cmake/Agbcc.cmake (included before this file),
  # which also arranges for tools/agbcc to be built from the ./agbcc
  # submodule as a build (not configure-time) dependency -- see there.
  set(CC1 ${AGBCC_DIR}/bin/agbcc)
  set(OLD_CC1 ${AGBCC_DIR}/bin/old_agbcc)
  set(ARM_CC1 ${AGBCC_DIR}/bin/agbcc_arm)

  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    find_program(CROSS_CPP arm-none-eabi-cpp REQUIRED)
  else()
    set(CROSS_CPP "${CMAKE_C_COMPILER} -E")
  endif()

  set(CPPFLAGS -iquote ${CMAKE_SOURCE_DIR}/include -Wno-trigraphs -DMODERN=${MODERN_INT}
               -I ${AGBCC_DIR}/include -I ${AGBCC_DIR} -nostdinc -undef -std=gnu89)
  set(CFLAGS -mthumb-interwork -Wimplicit -Wparentheses -Werror -O${O_LEVEL} -fhex-asm -g)

  set(LIB -L${AGBCC_DIR}/lib -lgcc -lc -L${LIBAGBSYSCALL_DIR} -lagbsyscall)
else()
  find_program(CROSS_GCC arm-none-eabi-gcc REQUIRED)
  find_program(CROSS_CPP arm-none-eabi-cpp REQUIRED)

  execute_process(
    COMMAND ${CROSS_GCC} --print-prog-name=cc1
    OUTPUT_VARIABLE _cc1_path OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(CC1 ${_cc1_path} -quiet)

  execute_process(COMMAND ${CROSS_GCC} -mthumb -print-file-name=libgcc.a
    OUTPUT_VARIABLE _libgcc_a OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${CROSS_GCC} -mthumb -print-file-name=libnosys.a
    OUTPUT_VARIABLE _libnosys_a OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${CROSS_GCC} -mthumb -print-file-name=libc.a
    OUTPUT_VARIABLE _libc_a OUTPUT_STRIP_TRAILING_WHITESPACE)
  get_filename_component(_libgcc_dir ${_libgcc_a} DIRECTORY)
  get_filename_component(_libnosys_dir ${_libnosys_a} DIRECTORY)
  get_filename_component(_libc_dir ${_libc_a} DIRECTORY)

  set(CPPFLAGS -iquote ${CMAKE_SOURCE_DIR}/include -Wno-trigraphs -DMODERN=${MODERN_INT})
  set(CFLAGS -mthumb -mthumb-interwork -O${O_LEVEL} -mabi=apcs-gnu -mtune=arm7tdmi
             -march=armv4t -fno-toplevel-reorder -Wno-pointer-to-int-cast)

  set(LIB -L${_libgcc_dir} -L${_libnosys_dir} -L${_libc_dir}
          -lc -lnosys -lgcc -L${LIBAGBSYSCALL_DIR} -lagbsyscall)
endif()

if(DINFO)
  list(APPEND CFLAGS -g)
endif()

# Per-object CC1/CFLAGS overrides, ported from the Makefile (search it for
# these exact object names to see the originals). Format:
# "objname|KIND|value", where a multi-word value is ','-joined: CMake
# lists are ';'-joined, so a ';' here would be silently flattened into
# separate _overrides entries by list(APPEND) instead of staying inside
# this one entry's value field.
set(_overrides)
if(NOT MODERN)
  list(APPEND _overrides "libc.o|CC1|${OLD_CC1}")
  list(APPEND _overrides "libc.o|CFLAGS_SET|-O2")
  list(APPEND _overrides "siirtc.o|CFLAGS_SET|-mthumb-interwork")
  list(APPEND _overrides "agb_flash.o|CFLAGS_SET|-O,-mthumb-interwork")
  list(APPEND _overrides "agb_flash_1m.o|CFLAGS_SET|-O,-mthumb-interwork")
  list(APPEND _overrides "agb_flash_mx.o|CFLAGS_SET|-O,-mthumb-interwork")
  list(APPEND _overrides "m4a.o|CC1|${OLD_CC1}")
  list(APPEND _overrides "record_mixing.o|CFLAGS_APPEND|-ffreestanding")
  list(APPEND _overrides "librfu_intr.o|CC1|${ARM_CC1}")
  list(APPEND _overrides "librfu_intr.o|CFLAGS_SET|-O2,-mthumb-interwork,-quiet")
else()
  list(APPEND _overrides "librfu_intr.o|CFLAGS_SET|-mthumb-interwork,-O2,-mabi=apcs-gnu,-mtune=arm7tdmi,-march=armv4t,-fno-toplevel-reorder,-Wno-pointer-to-int-cast")
  list(APPEND _overrides "berry_crush.o|CFLAGS_APPEND|-Wno-address-of-packed-member")
endif()

# Outputs are plain space-joined strings (not CMake lists), ready to pass
# as a single quoted argument to the compile_*.sh wrapper scripts.
function(_pokeemerald_object_cc1_cflags relname out_cc1 out_cflags)
  set(cc1 ${CC1})
  set(cflags ${CFLAGS})
  foreach(entry ${_overrides})
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 objname)
    if(objname STREQUAL relname)
      list(GET parts 1 kind)
      list(GET parts 2 rawvalue)
      string(REPLACE "," ";" value "${rawvalue}")
      if(kind STREQUAL "CC1")
        set(cc1 ${value})
      elseif(kind STREQUAL "CFLAGS_SET")
        set(cflags ${value})
      elseif(kind STREQUAL "CFLAGS_APPEND")
        list(APPEND cflags ${value})
      endif()
    endif()
  endforeach()
  list(JOIN cc1 " " cc1_str)
  list(JOIN cflags " " cflags_str)
  set(${out_cc1} "${cc1_str}" PARENT_SCOPE)
  set(${out_cflags} "${cflags_str}" PARENT_SCOPE)
endfunction()

list(JOIN CPPFLAGS " " CPPFLAGS_STR)
list(JOIN ASFLAGS " " ASFLAGS_STR)

# --- Object compilation ---
set(ALL_OBJS)

foreach(src ${C_SRCS})
  file(RELATIVE_PATH relname ${CMAKE_SOURCE_DIR}/src ${src})
  string(REGEX REPLACE "\\.c$" ".o" relobj ${relname})
  set(obj ${C_BUILDDIR}/${relobj})
  get_filename_component(objdir ${obj} DIRECTORY)

  _pokeemerald_object_cc1_cflags(${relobj} obj_cc1 obj_cflags)

  add_custom_command(
    OUTPUT ${obj}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${objdir}
    COMMAND bash ${CMAKE_SOURCE_DIR}/cmake/compile_c.sh
            "${CROSS_CPP}" "${CPPFLAGS_STR}"
            "$<TARGET_FILE:preproc>" "${ASSETS_DIR}" "${CHARMAP}"
            "${obj_cc1}" "${obj_cflags}"
            "${CROSS_AS}" "${ASFLAGS_STR}"
            "${src}" "${obj}"
    DEPENDS ${src} preproc generate_assets ${AGBCC_DEP}
    # preproc resolves INCBIN_U8/U16/U32 paths (as opposed to INCGFX_*,
    # which get the -g assets-root prefix) relative to its own CWD, which
    # the Makefile leaves at the repo root.
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "CC ${relname}"
    VERBATIM
  )
  list(APPEND ALL_OBJS ${obj})
endforeach()

foreach(src ${C_ASM_SRCS})
  file(RELATIVE_PATH relname ${CMAKE_SOURCE_DIR}/src ${src})
  string(REGEX REPLACE "\\.s$" ".o" relobj ${relname})
  set(obj ${C_BUILDDIR}/${relobj})
  get_filename_component(objdir ${obj} DIRECTORY)
  add_custom_command(
    OUTPUT ${obj}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${objdir}
    COMMAND bash ${CMAKE_SOURCE_DIR}/cmake/compile_asm_c.sh
            "$<TARGET_FILE:preproc>" "${CROSS_CPP}" "${CMAKE_SOURCE_DIR}/include"
            "${CHARMAP}" "${CROSS_AS}" "${ASFLAGS_STR}" "${src}" "${obj}"
    DEPENDS ${src} preproc generate_assets
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "AS ${relname}"
    VERBATIM
  )
  list(APPEND ALL_OBJS ${obj})
endforeach()

foreach(src ${ASM_SRCS})
  file(RELATIVE_PATH relname ${CMAKE_SOURCE_DIR}/asm ${src})
  string(REGEX REPLACE "\\.s$" ".o" relobj ${relname})
  set(obj ${ASM_BUILDDIR}/${relobj})
  get_filename_component(objdir ${obj} DIRECTORY)
  add_custom_command(
    OUTPUT ${obj}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${objdir}
    COMMAND ${CROSS_AS} ${ASFLAGS} -o ${obj} ${src}
    DEPENDS ${src}
    COMMENT "AS ${relname}"
    VERBATIM
  )
  list(APPEND ALL_OBJS ${obj})
endforeach()

foreach(src ${DATA_ASM_SRCS})
  file(RELATIVE_PATH relname ${CMAKE_SOURCE_DIR}/data ${src})
  string(REGEX REPLACE "\\.s$" ".o" relobj ${relname})
  set(obj ${DATA_ASM_BUILDDIR}/${relobj})
  get_filename_component(objdir ${obj} DIRECTORY)
  add_custom_command(
    OUTPUT ${obj}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${objdir}
    COMMAND bash ${CMAKE_SOURCE_DIR}/cmake/compile_asm_c.sh
            "$<TARGET_FILE:preproc>" "${CROSS_CPP}" "${CMAKE_SOURCE_DIR}/include"
            "${CHARMAP}" "${CROSS_AS}" "${ASFLAGS_STR}" "${src}" "${obj}"
    DEPENDS ${src} preproc generate_assets
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "AS ${relname}"
    VERBATIM
  )
  list(APPEND ALL_OBJS ${obj})
endforeach()

foreach(src ${MID_SRCS})
  file(RELATIVE_PATH relname ${CMAKE_SOURCE_DIR}/sound/songs/midi ${src})
  string(REGEX REPLACE "\\.mid$" ".s" relasm ${relname})
  string(REGEX REPLACE "\\.mid$" ".o" relobj ${relname})
  set(asmsrc ${CMAKE_SOURCE_DIR}/sound/songs/midi/${relasm})
  set(obj ${MID_BUILDDIR}/${relobj})
  get_filename_component(objdir ${obj} DIRECTORY)
  add_custom_command(
    OUTPUT ${obj}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${objdir}
    COMMAND ${CROSS_AS} ${ASFLAGS} -I ${CMAKE_SOURCE_DIR}/sound -o ${obj} ${asmsrc}
    DEPENDS ${src} generate_assets
    COMMENT "AS midi/${relname}"
    VERBATIM
  )
  list(APPEND ALL_OBJS ${obj})
endforeach()

# --- Linker symbol scripts (matching build only) ---
set(LD_SCRIPT_DEPS)
if(NOT MODERN)
  set(sym_bss_ld ${OBJ_DIR}/sym_bss.ld)
  set(sym_common_ld ${OBJ_DIR}/sym_common.ld)
  set(sym_ewram_ld ${OBJ_DIR}/sym_ewram.ld)

  add_custom_command(
    OUTPUT ${sym_bss_ld}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OBJ_DIR}
    COMMAND $<TARGET_FILE:ramscrgen> .bss ${CMAKE_SOURCE_DIR}/sym_bss.txt ENGLISH > ${sym_bss_ld}
    DEPENDS ${CMAKE_SOURCE_DIR}/sym_bss.txt ramscrgen
    VERBATIM
  )
  file(GLOB _common_syms_txts ${CMAKE_SOURCE_DIR}/common_syms/*.txt)
  add_custom_command(
    OUTPUT ${sym_common_ld}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OBJ_DIR}
    COMMAND $<TARGET_FILE:ramscrgen> COMMON ${CMAKE_SOURCE_DIR}/sym_common.txt ENGLISH
            -c ${C_BUILDDIR},${CMAKE_SOURCE_DIR}/common_syms > ${sym_common_ld}
    DEPENDS ${CMAKE_SOURCE_DIR}/sym_common.txt ${_common_syms_txts} ${ALL_OBJS} ramscrgen
    VERBATIM
  )
  add_custom_command(
    OUTPUT ${sym_ewram_ld}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OBJ_DIR}
    COMMAND $<TARGET_FILE:ramscrgen> ewram_data ${CMAKE_SOURCE_DIR}/sym_ewram.txt ENGLISH > ${sym_ewram_ld}
    DEPENDS ${CMAKE_SOURCE_DIR}/sym_ewram.txt ramscrgen
    VERBATIM
  )
  set(LD_SCRIPT_DEPS ${sym_bss_ld} ${sym_common_ld} ${sym_ewram_ld})
endif()

# --- Link + gbafix + objcopy ---
# The linker script places sections with wildcards like `src/*.o(.text)`
# that are resolved relative to ld's own working directory, and lists
# individual object files by that same relative path (e.g.
# `src/main.o(.text)`) for deterministic ROM layout. So, exactly like the
# original Makefile, we `cd` into OBJ_DIR and pass every object as a path
# relative to it, rather than compute this some more "obvious" way.
set(OBJS_REL)
foreach(obj ${ALL_OBJS})
  file(RELATIVE_PATH relobj ${OBJ_DIR} ${obj})
  list(APPEND OBJS_REL ${relobj})
endforeach()

file(RELATIVE_PATH LD_SCRIPT_REL ${OBJ_DIR} ${LD_SCRIPT})
file(RELATIVE_PATH MAP_REL ${OBJ_DIR} ${MAP})
file(RELATIVE_PATH ELF_REL ${OBJ_DIR} ${ELF})

add_custom_command(
  OUTPUT ${ELF}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${OBJ_DIR}
  COMMAND ${CMAKE_COMMAND} -E chdir ${OBJ_DIR}
          ${CROSS_LD} -Map ${MAP_REL} -T ${LD_SCRIPT_REL} --print-memory-usage
          -o ${ELF_REL} ${OBJS_REL} ${LIB}
  COMMAND $<TARGET_FILE:gbafix> ${ELF} -t${TITLE} -c${GAME_CODE} -m${MAKER_CODE} -r${REVISION} --silent
  DEPENDS ${LD_SCRIPT} ${LD_SCRIPT_DEPS} ${ALL_OBJS} libagbsyscall gbafix ${AGBCC_DEP}
  COMMENT "Linking ${ROM_NAME}"
  VERBATIM
)

add_custom_command(
  OUTPUT ${ROM}
  COMMAND ${CROSS_OBJCOPY} -O binary ${ELF} ${ROM}
  COMMAND $<TARGET_FILE:gbafix> ${ROM} -p --silent
  DEPENDS ${ELF} gbafix
  COMMENT "Building ${ROM_NAME}"
  VERBATIM
)

add_custom_target(rom ALL DEPENDS ${ROM})

add_custom_command(
  OUTPUT ${SYM}
  COMMAND bash ${CMAKE_SOURCE_DIR}/cmake/gen_syms.sh
          "${CROSS_OBJDUMP}" "${PERL_EXECUTABLE}" "${ELF}" "${SYM}"
  DEPENDS ${ELF}
  COMMENT "Generating ${ROM_NAME}.sym"
  VERBATIM
)
add_custom_target(syms DEPENDS ${SYM})

if(NOT MODERN)
  add_custom_target(compare
    COMMAND ${SHA1_EXECUTABLE} ${CMAKE_SOURCE_DIR}/rom.sha1
    DEPENDS rom
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Verifying ${ROM_NAME} matches rom.sha1"
    VERBATIM
  )
endif()
