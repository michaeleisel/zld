add_tapi_test_library(CPP1
  FRAMEWORK
  PUBLIC_HEADERS CPP.h
  SOURCE CPP1.cpp
)
target_compile_features(CPP1 PRIVATE cxx_std_11)
