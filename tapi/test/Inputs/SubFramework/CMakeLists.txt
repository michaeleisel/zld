add_tapi_test_library(SubFramework
  FRAMEWORK
  PUBLIC_HEADERS SubFramework.h
  SOURCE SubFramework.c
)
target_link_libraries(SubFramework PRIVATE "-umbrella test")
