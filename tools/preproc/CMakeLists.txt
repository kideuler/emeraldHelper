add_executable(preproc
  asm_file.cpp
  c_file.cpp
  charmap.cpp
  preproc.cpp
  string_parser.cpp
  utf8.cpp
  io.cpp
)
target_compile_features(preproc PRIVATE cxx_std_11)
if(NOT MSVC)
  target_compile_options(preproc PRIVATE -Wall -Wno-switch -Werror)
endif()
