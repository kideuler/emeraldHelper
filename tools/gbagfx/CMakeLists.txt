find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(gbagfx
  main.c
  convert_png.c
  gfx.c
  jasc_pal.c
  lz.c
  rl.c
  util.c
  font.c
  huff.c
)
target_compile_features(gbagfx PRIVATE c_std_11)
target_compile_definitions(gbagfx PRIVATE PNG_SKIP_SETJMP_CHECK)
if(NOT MSVC)
  target_compile_options(gbagfx PRIVATE -Wall -Wextra -Werror -Wno-sign-compare)
endif()
target_link_libraries(gbagfx PRIVATE PNG::PNG ZLIB::ZLIB)
