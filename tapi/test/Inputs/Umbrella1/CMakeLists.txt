add_tapi_test_library(Umbrella1
  FRAMEWORK
  PUBLIC_HEADERS
  Headers/AAA.h
  Headers/AAAUmbrella1.h
  Headers/Umbrella1.h
  Headers/Umbrella1_Private.h

  PRIVATE_HEADERS
  PrivateHeaders/AAAA.h
  PrivateHeaders/AAAAUmbrella1Private.h
  PrivateHeaders/Umbrella1Private.h

  SOURCE Umbrella1.m
)
