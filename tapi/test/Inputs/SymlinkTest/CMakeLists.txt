add_tapi_test_library(SymlinkTest
  SOURCE system.c
  OUTPUT_NAME System
  OUTPUT_DIRECTORY_PREFIX symlinktest
)
add_custom_command(TARGET SymlinkTest
  POST_BUILD
  WORKING_DIRECTORY ${TAPI_BINARY_DIR}/Inputs/symlinktest
  COMMAND ${CMAKE_COMMAND} -E create_symlink /usr/lib/libSystem.dylib usr/lib/libfoo.dylib
)
