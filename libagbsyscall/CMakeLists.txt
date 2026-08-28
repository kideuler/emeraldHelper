# Builds libagbsyscall.a: one tiny .o per BIOS syscall wrapper, all
# assembled from the same libagbsyscall.s with a different --defsym
# selecting which wrapper is active, then archived together.
# Requires CROSS_AS / CROSS_AR (arm-none-eabi-as / -ar) from the parent scope.

set(AGBSYSCALL_LIST
  IntrWait RegisterRamReset Sqrt MusicPlayerOpen SoundBiasReset
  SoundDriverVSyncOn Mod VBlankIntrWait MusicPlayerStart SoundDriverVSyncOff
  HuffUnComp SoftResetExram MusicPlayerFadeOut LZ77UnCompWram SoundDriverMain
  SoundBiasChange LZ77UnCompVram ArcTan2 MusicPlayerStop DivArm ModArm
  SoundDriverVSync SoundDriverInit BgAffineSet Diff8bitUnFilterWram MultiBoot
  MidiKey2Freq Div Diff8bitUnFilterVram ArcTan ObjAffineSet SoftResetRom
  SoundDriverMode RLUnCompWram BitUnPack SoundChannelClear CpuFastSet CpuSet
  Diff16bitUnFilter SoundBiasSet MusicPlayerContinue SoftReset RLUnCompVram
)

set(AGBSYSCALL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/libagbsyscall.s)
# Built in-tree at the same path the original Makefile used
# (libagbsyscall/libagbsyscall.a), so the final link step's -L path is a
# simple, obviously-correct absolute path rather than something computed
# relative to whichever OBJ_DIR the active build variant uses.
set(AGBSYSCALL_OBJDIR ${CMAKE_CURRENT_BINARY_DIR}/obj)
file(MAKE_DIRECTORY ${AGBSYSCALL_OBJDIR})

set(AGBSYSCALL_OBJS)
foreach(syscall ${AGBSYSCALL_LIST})
  set(obj ${AGBSYSCALL_OBJDIR}/${syscall}.o)
  add_custom_command(
    OUTPUT ${obj}
    COMMAND ${CROSS_AS} -mcpu=arm7tdmi --defsym L_${syscall}=1 -o ${obj} ${AGBSYSCALL_SRC}
    DEPENDS ${AGBSYSCALL_SRC}
    # libagbsyscall.s uses paths like "../constants/gba_constants.inc",
    # resolved by `as` relative to its own CWD (matching the original
    # Makefile's `$(MAKE) -C libagbsyscall`, which put `as` there too) —
    # not relative to CMAKE_CURRENT_BINARY_DIR, the default CWD here.
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "AS libagbsyscall/${syscall}.o"
    VERBATIM
  )
  list(APPEND AGBSYSCALL_OBJS ${obj})
endforeach()

set(AGBSYSCALL_LIB ${CMAKE_CURRENT_SOURCE_DIR}/libagbsyscall.a)
add_custom_command(
  OUTPUT ${AGBSYSCALL_LIB}
  COMMAND ${CROSS_AR} rc ${AGBSYSCALL_LIB} ${AGBSYSCALL_OBJS}
  DEPENDS ${AGBSYSCALL_OBJS}
  COMMENT "AR libagbsyscall.a"
  VERBATIM
)

add_custom_target(libagbsyscall DEPENDS ${AGBSYSCALL_LIB})
set(LIBAGBSYSCALL_PATH ${AGBSYSCALL_LIB} CACHE INTERNAL "Path to built libagbsyscall.a")
set(LIBAGBSYSCALL_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE INTERNAL "Directory containing libagbsyscall.a")
