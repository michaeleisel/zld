add_tapi_test_library(FooCC
  FRAMEWORK
  SOURCE Foo.m
)
target_compile_options(FooCC PRIVATE -fprofile-instr-generate)
target_link_libraries(FooCC PRIVATE -fprofile-instr-generate)
