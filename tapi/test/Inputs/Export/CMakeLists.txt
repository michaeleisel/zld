add_tapi_test_library(Export
  FRAMEWORK
  PUBLIC_HEADERS Export.h
  SOURCE Export.m NonExport.m
  OUTPUT_DIRECTORY_PREFIX Export
)
target_compile_options(Export
  PRIVATE
  -Wno-objc-root-class
)
target_link_libraries(Export
  PRIVATE
  "-framework Foundation"
)
