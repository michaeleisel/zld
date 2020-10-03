set(CMAKE_OSX_DEPLOYMENT_TARGET 10.10)
add_tapi_test_library(Simple
  FRAMEWORK

  PUBLIC_HEADERS
  Simple.h
  Basic.h
  External.h
  SimpleAPI.h

  PRIVATE_HEADERS
  SimplePrivate.h
  SimplePrivateSPI.h

  SOURCE Simple.m
  CURRENT_VERSION 1.2.3.4.5
)
target_compile_options(Simple
  PRIVATE
  -w
)
target_link_libraries(Simple
  PRIVATE
  "-framework Foundation"
  "-framework CoreData"
)
