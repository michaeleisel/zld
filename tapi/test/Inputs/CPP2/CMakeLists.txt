add_tapi_test_library(CPP2
  FRAMEWORK
  PUBLIC_HEADERS Templates.h
  SOURCE CPP2.cpp
)
target_compile_features(CPP2 PRIVATE cxx_std_11)
