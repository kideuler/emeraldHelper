# Builds the vendored ./agbcc submodule and installs it to tools/agbcc,
# the layout Rom.cmake's matching (agbcc) build expects (CC1/OLD_CC1/
# ARM_CC1 under bin/, headers under include/, libgcc.a+libc.a under
# lib/). This replaces the manual `cd agbcc && ./build.sh &&
# ./install.sh ..` step from INSTALL.md with a `cmake --build` dependency
# -- see build_agbcc.sh for why that's a thin wrapper around agbcc's own
# scripts rather than a from-scratch CMake translation of its build.
#
# No-op under MODERN=ON: the modern arm-none-eabi-gcc build doesn't use
# agbcc at all.
if(NOT MODERN)
  set(AGBCC_SRC_DIR ${CMAKE_SOURCE_DIR}/agbcc)
  set(AGBCC_DIR ${CMAKE_SOURCE_DIR}/tools/agbcc)

  if(NOT EXISTS ${AGBCC_SRC_DIR}/build.sh)
    message(FATAL_ERROR
      "agbcc submodule not checked out (${AGBCC_SRC_DIR}/build.sh missing). "
      "Run: git submodule update --init agbcc")
  endif()

  find_program(SH_EXECUTABLE sh REQUIRED)

  set(AGBCC_OUTPUTS
    ${AGBCC_DIR}/bin/agbcc
    ${AGBCC_DIR}/bin/old_agbcc
    ${AGBCC_DIR}/bin/agbcc_arm
    ${AGBCC_DIR}/lib/libgcc.a
    ${AGBCC_DIR}/lib/libc.a
  )

  # Re-run the (couple-minutes) build when agbcc's checked-out commit
  # changes or its own build/install scripts do -- not on every
  # configure, and without an expensive recursive glob over its sources.
  set(_agbcc_triggers ${AGBCC_SRC_DIR}/build.sh ${AGBCC_SRC_DIR}/install.sh)
  if(EXISTS ${CMAKE_SOURCE_DIR}/.git/modules/agbcc/HEAD)
    list(APPEND _agbcc_triggers ${CMAKE_SOURCE_DIR}/.git/modules/agbcc/HEAD)
  endif()

  add_custom_command(
    OUTPUT ${AGBCC_OUTPUTS}
    COMMAND ${SH_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/build_agbcc.sh
            ${AGBCC_SRC_DIR} ${CMAKE_SOURCE_DIR}
    DEPENDS ${_agbcc_triggers}
    COMMENT "Building agbcc (old_agbcc/agbcc/agbcc_arm + libgcc.a/libc.a) from ./agbcc"
    VERBATIM
  )
  add_custom_target(agbcc_toolchain DEPENDS ${AGBCC_OUTPUTS})
endif()
