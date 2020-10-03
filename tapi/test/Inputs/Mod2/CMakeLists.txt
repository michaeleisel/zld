set_property(
  SOURCE module.modulemap
  PROPERTY MACOSX_PACKAGE_LOCATION "Modules"
)
add_tapi_test_library(Mod2
  FRAMEWORK
  PUBLIC_HEADERS Mod2.h A.h B.h C.h
  SOURCE Mod2.m module.modulemap
)
add_custom_command(TARGET Mod2
  POST_BUILD
  WORKING_DIRECTORY ${TAPI_BINARY_DIR}/Inputs/System/Library/Frameworks/Mod2.framework
  COMMAND ${CMAKE_COMMAND} -E create_symlink Versions/Current/Modules Modules
)
