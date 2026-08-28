# Host-side tool executables used to preprocess assets and sources during
# the ROM build. These always build with the host's native compiler,
# regardless of which GBA toolchain/variant the top-level project targets.
add_subdirectory(bin2c)
add_subdirectory(gbafix)
add_subdirectory(gbagfx)
add_subdirectory(jsonproc)
add_subdirectory(mapjson)
add_subdirectory(mid2agb)
add_subdirectory(preproc)
add_subdirectory(ramscrgen)
add_subdirectory(rsfont)
add_subdirectory(scaninc)
add_subdirectory(wav2agb)
