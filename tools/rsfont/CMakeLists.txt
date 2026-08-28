find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(rsfont
  main.c
  convert_png.c
  util.c
  font.c
)
target_compile_features(rsfont PRIVATE c_std_11)
target_compile_definitions(rsfont PRIVATE PNG_SKIP_SETJMP_CHECK)
if(NOT MSVC)
  target_compile_options(rsfont PRIVATE -Wall -Wextra -Werror)
endif()
target_link_libraries(rsfont PRIVATE PNG::PNG ZLIB::ZLIB)
