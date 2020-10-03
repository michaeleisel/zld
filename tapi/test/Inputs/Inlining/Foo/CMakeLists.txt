add_tapi_test_library(InliningFoo
  FRAMEWORK
  PUBLIC_HEADERS Foo.h
  SOURCE Foo.m
  ARCHITECTURES x86_64
  INSTALL_NAME_DIR /System/Library/Frameworks/Inlining.framework/Versions/A/Frameworks
  OUTPUT_DIRECTORY_SUFFIX Inlining.framework/Versions/A/Frameworks
  OUTPUT_NAME Foo
)
