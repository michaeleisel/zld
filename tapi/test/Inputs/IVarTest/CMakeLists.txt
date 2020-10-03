add_tapi_test_library(IVarTest
  FRAMEWORK
  PUBLIC_HEADERS IVarTest.h
  SOURCE IVarTest.m
  ARCHITECTURES x86_64
)
target_compile_options(IVarTest PRIVATE -fobjc-arc)
target_link_libraries(IVarTest PRIVATE "-framework Foundation")
