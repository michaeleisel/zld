add_tapi_test_library(ReexportSourceLibAll
  SOURCE Source.m
  ARCHITECTURES x86_64
  INSTALL_NAME_DIR /System/Library/Frameworks/ReexportAll.framework/Versions/A/Frameworks
  OUTPUT_DIRECTORY_PREFIX ReexportAll
  OUTPUT_DIRECTORY_SUFFIX ReexportAll.framework/Versions/A/Frameworks
  OUTPUT_DIRECTORY ReexportAll/System/Library/Frameworks/ReexportAll.framework/Versions/A/Frameworks
  OUTPUT_NAME SourceAll
)
target_link_libraries(ReexportSourceLibAll PRIVATE objc)
