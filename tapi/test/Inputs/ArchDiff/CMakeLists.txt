add_tapi_test_library(ArchDiff
  FRAMEWORK
  PUBLIC_HEADERS arch-diff.h
  SOURCE arch-diff.c
  ARCHITECTURES x86_64 x86_64h
  OUTPUT_DIRECTORY_PREFIX ArchDiff
)
