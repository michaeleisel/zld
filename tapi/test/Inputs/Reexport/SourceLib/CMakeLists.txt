add_tapi_test_library(ReexportSourceLib
  SOURCE Source.m
  ARCHITECTURES x86_64
  INSTALL_NAME_DIR /System/Library/Frameworks/Reexport.framework/Versions/A/Frameworks
  OUTPUT_NAME Source
  OUTPUT_DIRECTORY System/Library/Frameworks/Reexport.framework/Versions/A/Frameworks
)
target_link_libraries(ReexportSourceLib PRIVATE objc)
