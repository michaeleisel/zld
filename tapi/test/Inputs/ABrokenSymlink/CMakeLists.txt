add_tapi_test_library(ABrokenSymlink
  FRAMEWORK
  PUBLIC_HEADERS ABrokenSymlink.h
  SOURCE ABrokenSymlink.m
)
add_custom_command(TARGET ABrokenSymlink
  POST_BUILD
  WORKING_DIRECTORY ${TAPI_BINARY_DIR}/Inputs/System/Library/Frameworks/ABrokenSymlink.framework
  COMMAND ${CMAKE_COMMAND} -E create_symlink Versions/A/ABrokenSymlink.tbd AAA.tbd
  COMMAND ${CMAKE_COMMAND} -E create_symlink Versions/A/ABrokenSymlink.tbd BBB.tbd
  COMMAND ${CMAKE_COMMAND} -E create_symlink no_such_file.h Versions/A/Headers/AAA.h
)
