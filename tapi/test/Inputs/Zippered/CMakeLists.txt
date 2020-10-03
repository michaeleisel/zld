add_tapi_test_library(Zippered
  FRAMEWORK
  PUBLIC_HEADERS Zippered.h
  PRIVATE_HEADERS Zippered_Private.h
  SOURCE Zippered.m
)
target_compile_options(Zippered
  PRIVATE
  -target x86_64-apple-macosx10.15
  -F${TAPI_BINARY_DIR}/Inputs/System/Library/Frameworks
)
target_link_libraries(Zippered
  PRIVATE
  objc
  "-target x86_64-apple-macosx10.15"
  -Wl,-iosmac_version_min,13.0
)
add_dependencies(Zippered ZipperedAPI)
