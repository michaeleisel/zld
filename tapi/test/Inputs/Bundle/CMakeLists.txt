add_library(Bundle MODULE bundle.m)
set_target_properties(Bundle
  PROPERTIES
  OSX_ARCHITECTURES "x86_64;x86_64h"
  C_COMPILER_LAUNCHER ""
  CXX_COMPILER_LAUNCHER ""
)
target_link_libraries(Bundle
  PRIVATE
  objc
  "-framework Foundation"
  "-framework CoreData"
)
set_target_properties(Bundle
  PROPERTIES
  PREFIX ""
  SUFFIX ""
  LIBRARY_OUTPUT_DIRECTORY "${TAPI_BINARY_DIR}/Inputs/Bundle/System/Library/Bundles"
)
add_dependencies(TapiTestInputs Bundle)
