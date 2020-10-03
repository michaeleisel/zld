add_tapi_test_library(Umbrella2
  FRAMEWORK
  PUBLIC_HEADERS
  Headers/AAA.h
  Headers/SpecialUmbrella.h

  PRIVATE_HEADERS
  PrivateHeaders/AAAA.h
  PrivateHeaders/SpecialPrivateUmbrella.h
  SOURCE Umbrella2.m
)
