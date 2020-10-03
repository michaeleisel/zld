add_tapi_test_library(IVarExtensionTest
  FRAMEWORK
  PUBLIC_HEADERS IVarExtensionTest.h
  PRIVATE_HEADERS IVarExtensionTest_Private.h
  SOURCE IVarExtensionTest.m
)
target_compile_options(IVarExtensionTest PRIVATE -fobjc-arc)
target_link_libraries(IVarExtensionTest PRIVATE "-framework Foundation")
