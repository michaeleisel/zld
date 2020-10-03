add_subdirectory(Reexports)

add_tapi_test_library(UmbrellaCC
  FRAMEWORK
  PUBLIC_HEADERS UmbrellaCC.h
  SOURCE UmbrellaCC.m
)
target_compile_options(UmbrellaCC
  PRIVATE
  -fprofile-instr-generate
)
target_link_libraries(UmbrellaCC
  PRIVATE
  -fprofile-instr-generate
  -F${TAPI_BINARY_DIR}/Inputs/System/Library/Frameworks
  -Wl,-reexport_framework,FooCC
)
add_dependencies(UmbrellaCC FooCC)
