add_tapi_test_library(Mod
  FRAMEWORK
  PUBLIC_HEADERS Mod.h
  SOURCE Mod.m
)
target_compile_options(Mod
  PRIVATE
  -fmodules
  -fmodules-validate-system-headers
)
target_link_libraries(Mod PRIVATE "-framework Foundation")
