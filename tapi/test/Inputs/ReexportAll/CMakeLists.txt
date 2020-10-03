add_subdirectory(SourceLib)

add_tapi_test_library(ReexportAll
  FRAMEWORK
  PUBLIC_HEADERS Header1.h Header2.h Header3.h
  SOURCE Reexport.m
  ARCHITECTURES x86_64
  OUTPUT_DIRECTORY_PREFIX ReexportAll
)
target_link_libraries(ReexportAll
  PRIVATE
  -Wl,-reexport_library,${TAPI_BINARY_DIR}/Inputs/ReexportAll/System/Library/Frameworks/ReexportAll.framework/Versions/A/Frameworks/libSourceAll.dylib
)
add_dependencies(ReexportAll ReexportSourceLibAll)
