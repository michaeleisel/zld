add_subdirectory(SourceLib)

add_tapi_test_library(Reexport
  FRAMEWORK
  PUBLIC_HEADERS Header1.h Header2.h Header3.h
  SOURCE Reexport.m
  ARCHITECTURES x86_64
)
target_link_libraries(Reexport
  PRIVATE
  ReexportSourceLib
  -Xarch_x86_64 -Wl,-reexported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/reexport.x86_64.symbols
)
