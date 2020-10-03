add_tapi_test_library(AvailabilityTest
  FRAMEWORK
  PUBLIC_HEADERS AvailabilityTest.h
  PRIVATE_HEADERS AvailabilityTestPrivate.h
  SOURCE AvailabilityTest.m
  ARCHITECTURES x86_64 x86_64h
)
target_link_libraries(AvailabilityTest PRIVATE objc)
