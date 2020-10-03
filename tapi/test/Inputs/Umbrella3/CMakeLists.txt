add_tapi_test_library(Foo FRAMEWORK SOURCE Reexports/Foo.m)
add_tapi_test_library(Bar FRAMEWORK SOURCE Reexports/Bar.m)
add_tapi_test_library(Baz SOURCE Reexports/Baz.m)

add_tapi_test_library(Umbrella3
  FRAMEWORK
  PUBLIC_HEADERS
  Headers/Umbrella3.h
  Headers/Foo.h
  Headers/Bar.h
  Headers/Baz.h
  SOURCE Umbrella3.m
)
target_link_libraries(Umbrella3
  PRIVATE
  -F${TAPI_BINARY_DIR}/Inputs/System/Library/Frameworks
  -Wl,-reexport_framework,Foo
  -Wl,-reexport_framework,Bar
  -L${TAPI_BINARY_DIR}/Inputs/usr/lib
  -Wl,-reexport-lBaz
)
add_dependencies(Umbrella3 Foo Bar Baz)
