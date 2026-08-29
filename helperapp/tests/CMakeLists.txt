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
)
target_link_libraries(emerald_core_tests PRIVATE emerald_core GTest::gtest GTest::gtest_main)
target_compile_definitions(emerald_core_tests PRIVATE
    EMERALD_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../config")

include(GoogleTest)
gtest_discover_tests(emerald_core_tests)
