add_tapi_test_library(FooLib
  PUBLIC_HEADERS foo.h
  PRIVATE_HEADERS foo_private.h
  SOURCE foo.c
  OUTPUT_NAME foo
  OUTPUT_DIRECTORY_PREFIX dylib_dstroot
)
add_custom_command(TARGET FooLib
  POST_BUILD
  WORKING_DIRECTORY ${TAPI_BINARY_DIR}/Inputs/dylib_dstroot
  COMMAND mkdir -p ${TAPI_BINARY_DIR}/Inputs/dylib_dstroot/usr/include
  COMMAND mkdir -p ${TAPI_BINARY_DIR}/Inputs/dylib_dstroot/usr/local/include
  COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/foo.h usr/include/
  COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/foo_private.h usr/local/include/
)
