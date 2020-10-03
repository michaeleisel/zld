add_subdirectory(Bar)
add_subdirectory(Foo)

add_tapi_test_library(Inlining
  FRAMEWORK
  SOURCE Inlining.m
  ARCHITECTURES x86_64 x86_64h
)
target_link_libraries(Inlining
  PRIVATE
  -F${TAPI_BINARY_DIR}/Inputs/System/Library/Frameworks
  -F${TAPI_BINARY_DIR}/Inputs/System/Library/Frameworks/Inlining.framework/Versions/A/Frameworks
  -Wl,-reexport_framework,Bar
  -Wl,-reexport_framework,Foo
)
add_dependencies(Inlining InliningBar InliningFoo)
