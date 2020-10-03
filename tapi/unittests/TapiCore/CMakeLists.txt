set(INPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Inputs")
add_definitions(-DINPUT_PATH="${INPUT_PATH}")
add_tapi_unittest(TapiCoreTests
  FileListReader.cpp
  FileSystem.cpp
  Path.cpp
  Utils.cpp
  Reader.cpp
  )

target_link_libraries(TapiCoreTests
  PRIVATE
  tapiCore
  )
