add_tapi_test_library(CPP3
  FRAMEWORK
  PUBLIC_HEADERS VTable.h
  SOURCE CPP3.cpp
)
target_compile_features(CPP3 PRIVATE cxx_std_11)
