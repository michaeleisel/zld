add_tapi_test_library(IOSMac
  FRAMEWORK
  PUBLIC_HEADERS IOSMac.h
  SOURCE IOSMac.m
)
target_compile_options(IOSMac
  PRIVATE
  -target x86_64-apple-ios13.0-macabi
)
target_link_libraries(IOSMac
  PRIVATE
  objc
  "-target x86_64-apple-ios13.0-macabi"
)
