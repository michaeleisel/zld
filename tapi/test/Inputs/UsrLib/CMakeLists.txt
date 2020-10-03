add_tapi_test_library(Test
  PUBLIC_HEADERS umbrella.h test.h test_exclude.h test_exclude2.h
  PRIVATE_HEADERS test_private.h
  SOURCE test.c
  OUTPUT_DIRECTORY_PREFIX UsrLib
)
