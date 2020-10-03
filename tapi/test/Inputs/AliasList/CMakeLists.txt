add_tapi_test_library(AliasList
  FRAMEWORK

  PUBLIC_HEADERS
  AliasList.h

  PRIVATE_HEADERS
  AliasList_Private.h

  SOURCE AliasList.m
)
target_link_libraries(AliasList
  PRIVATE
  "-Wl,-alias_list,${CMAKE_CURRENT_SOURCE_DIR}/symbol_alias_list.txt"
)
