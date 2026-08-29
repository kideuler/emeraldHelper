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
)
target_link_libraries(emerald_calc_tests PRIVATE emerald_calc GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(emerald_core_tests)
gtest_discover_tests(emerald_calc_tests)
