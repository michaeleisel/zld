set(INPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../test/Inputs/unittests")
add_definitions(-DINPUT_PATH="${INPUT_PATH}")
add_tapi_unittest(libtapiTests
  APIVersionTest.cpp
  LinkerInterfaceFileTest_TBD_v1.cpp
  LinkerInterfaceFileTest_TBD_v2.cpp
  LinkerInterfaceFileTest_TBD_v3.cpp
  LinkerInterfaceFileTest_TBD_v4.cpp
  VersionTest.cpp
  )

target_link_libraries(libtapiTests
  PRIVATE
  libtapi
  )

set_property(TARGET libtapiTests APPEND_STRING
  PROPERTY
  LINK_FLAGS " -client_name ld"
  )
