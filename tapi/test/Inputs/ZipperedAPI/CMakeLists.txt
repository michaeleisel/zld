add_tapi_test_library(ZipperedAPI
  FRAMEWORK
  PUBLIC_HEADERS ZipperedAPI.h
  SOURCE ZipperedAPI.m
)
target_compile_options(ZipperedAPI
  PRIVATE
  -target x86_64-apple-macosx10.15
)
target_link_libraries(ZipperedAPI
  PRIVATE
  objc
  "-target x86_64-apple-macosx10.15"
  -Wl,-iosmac_version_min,13.0
)
