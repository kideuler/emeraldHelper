# Phase 2/3 offline tests: pure decode/transport logic, no emulator or GUI
# event loop in the loop (Testing strategy in CODE_PLAN.md).
find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    message(STATUS "helperapp: GTest not found, skipping unit tests")
    return()
endif()

add_executable(emerald_core_tests
    test_frameparser.cpp
    test_decoder.cpp
    test_partydecoder.cpp
    test_config.cpp
    test_battlerview.cpp
)
target_link_libraries(emerald_core_tests PRIVATE emerald_core GTest::gtest GTest::gtest_main)
target_compile_definitions(emerald_core_tests PRIVATE
    EMERALD_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../config")

# Phase 5 damage-calc tests. emerald_calc itself is plain C (no Qt/decode
# dependency) -- this driver is C++ only because GTest's TEST() macros need
# it; emerald_calc.h is extern "C"-guarded for exactly this.
add_executable(emerald_calc_tests
    test_damage.cpp
    test_decomp_diff.cpp
    decomp_reference.c
    ${CMAKE_CURRENT_BINARY_DIR}/decomp_reference/decomp_reference_macros.inc
    ${CMAKE_CURRENT_BINARY_DIR}/decomp_reference/decomp_reference_extract.inc
)
target_link_libraries(emerald_calc_tests PRIVATE emerald_calc GTest::gtest GTest::gtest_main)
# test_decomp_diff.cpp holds the port's internal commands (calc/src/
# battle_internal.h) against the originals.
target_include_directories(emerald_calc_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../calc/src
    ${CMAKE_CURRENT_BINARY_DIR}/decomp_reference)

# The differential test's reference side: the decomp's own battle code,
# copied verbatim out of src/ and include/ at build time (extract_decomp.py)
# -- so it's re-extracted whenever those files change, and can never be a
# hand-maintained copy that drifts. Macros/tables and functions go in
# separate files because decomp_reference.c needs the former before its
# stubs and the latter after.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_decomp_macros
    include/global.h:ARRAY_COUNT
    include/global.h:T2_READ_16
    include/battle.h:GET_BATTLER_SIDE
    include/battle.h:GET_BATTLER_SIDE2
    include/battle.h:DYNAMIC_TYPE_MASK
    include/battle.h:F_DYNAMIC_TYPE_IGNORE_PHYSICALITY
    include/battle.h:F_DYNAMIC_TYPE_SET
    include/battle.h:GET_MOVE_TYPE
    include/battle.h:IS_TYPE_PHYSICAL
    include/battle.h:IS_TYPE_SPECIAL
    include/battle.h:IS_BATTLER_OF_TYPE
    include/battle_main.h:TYPE_EFFECT_ATK_TYPE
    include/battle_main.h:TYPE_EFFECT_DEF_TYPE
    include/battle_main.h:TYPE_EFFECT_MULTIPLIER
    include/battle_main.h:TYPE_MUL_NO_EFFECT
    include/battle_main.h:TYPE_MUL_NOT_EFFECTIVE
    include/battle_main.h:TYPE_MUL_NORMAL
    include/battle_main.h:TYPE_MUL_SUPER_EFFECTIVE
    include/battle_main.h:TYPE_FORESIGHT
    include/battle_main.h:TYPE_ENDTABLE
    include/battle_util.h:ABILITYEFFECT_MOVES_BLOCK
    include/battle_util.h:ABILITYEFFECT_ABSORBING
    include/battle_util.h:ABILITYEFFECT_FIELD_SPORT
    include/battle_util.h:ABILITYEFFECT_CHECK_ON_FIELD
    include/battle_util.h:ABILITYEFFECT_MUD_SPORT
    include/battle_util.h:ABILITYEFFECT_WATER_SPORT
    include/battle_util.h:ABILITY_ON_FIELD
    include/battle_util.h:ABILITY_ON_FIELD2
    include/battle_util.h:WEATHER_HAS_EFFECT
    include/battle_util.h:WEATHER_HAS_EFFECT2
    src/battle_script_commands.c:DEFENDER_IS_PROTECTED
    src/pokemon.c:APPLY_STAT_MOD
    src/pokemon.c:gStatStageRatios
    src/pokemon.c:sHoldEffectToType
    src/battle_main.c:gTypeEffectiveness
    src/battle_script_commands.c:sCriticalHitChance
    src/battle_script_commands.c:sAccuracyStageRatios
    src/battle_script_commands.c:sFlailHpScaleToPowerTable
    src/battle_script_commands.c:sWeightToDamageTable
)
set(_decomp_functions
    src/battle_anim_mons.c:GetBattlerSide
    src/battle_anim_mons.c:GetBattlerPosition
    src/battle_interface.c:GetScaledHPFraction
    src/pokemon.c:CountAliveMonsInBattle
    src/pokemon.c:ShouldGetStatBadgeBoost
    src/pokemon.c:CalculateBaseDamage
    src/battle_script_commands.c:ModulateDmgByType
    src/battle_script_commands.c:AttacksThisTurn
    src/battle_script_commands.c:CheckWonderGuardAndLevitate
    src/battle_script_commands.c:JumpIfMoveFailed
    src/battle_script_commands.c:JumpIfMoveAffectedByProtect
    src/battle_script_commands.c:AccuracyCalcHelper
    src/battle_script_commands.c:Cmd_accuracycheck
    src/battle_script_commands.c:Cmd_critcalc
    src/battle_script_commands.c:Cmd_damagecalc
    src/battle_script_commands.c:Cmd_typecalc
    src/battle_script_commands.c:Cmd_weightdamagecalculation
    src/battle_script_commands.c:Cmd_remaininghptopower
    src/battle_script_commands.c:Cmd_scaledamagebyhealthratio
    src/battle_script_commands.c:Cmd_friendshiptodamagecalculation
    src/battle_script_commands.c:Cmd_hiddenpowercalc
    src/battle_script_commands.c:Cmd_setweatherballtype
)
set(_decomp_sources
    ${POKEEMERALD_ROOT}/include/global.h ${POKEEMERALD_ROOT}/include/battle.h
    ${POKEEMERALD_ROOT}/include/battle_main.h ${POKEEMERALD_ROOT}/include/battle_util.h
    ${POKEEMERALD_ROOT}/src/pokemon.c ${POKEEMERALD_ROOT}/src/battle_main.c
    ${POKEEMERALD_ROOT}/src/battle_script_commands.c ${POKEEMERALD_ROOT}/src/battle_anim_mons.c
    ${POKEEMERALD_ROOT}/src/battle_interface.c)
set(_decomp_out ${CMAKE_CURRENT_BINARY_DIR}/decomp_reference)
add_custom_command(
    OUTPUT ${_decomp_out}/decomp_reference_macros.inc ${_decomp_out}/decomp_reference_extract.inc
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_decomp_out}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_decomp.py
            ${POKEEMERALD_ROOT} ${_decomp_out}/decomp_reference_macros.inc ${_decomp_macros}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_decomp.py
            ${POKEEMERALD_ROOT} ${_decomp_out}/decomp_reference_extract.inc ${_decomp_functions}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/extract_decomp.py ${_decomp_sources}
    COMMENT "Extracting reference battle code from the decomp"
    VERBATIM)

include(GoogleTest)
gtest_discover_tests(emerald_core_tests)
gtest_discover_tests(emerald_calc_tests)
