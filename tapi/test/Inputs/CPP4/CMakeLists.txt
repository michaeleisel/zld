add_tapi_test_library(CPP4
  FRAMEWORK
  PUBLIC_HEADERS Templates.h
  SOURCE CPP4.cpp
)
target_compile_features(CPP4 PRIVATE cxx_std_11)
