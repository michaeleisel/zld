add_tapi_test_library(InliningBar
  FRAMEWORK
  PUBLIC_HEADERS Bar.h
  SOURCE Bar.m
  ARCHITECTURES x86_64
  INSTALL_NAME_DIR /System/Library/Frameworks/Inlining.framework/Versions/A/Frameworks
  OUTPUT_DIRECTORY_SUFFIX Inlining.framework/Versions/A/Frameworks
  OUTPUT_NAME Bar
)
target_link_libraries(InliningBar
  PRIVATE
  -F${TAPI_BINARY_DIR}/Inputs/System/Library/Frameworks
  -Xarch_x86_64 -Wl,-reexport_framework,Public
)
add_dependencies(InliningBar Public)
