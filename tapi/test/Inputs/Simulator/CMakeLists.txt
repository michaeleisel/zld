set(CMAKE_OSX_DEPLOYMENT_TARGET "" CACHE STRING "Must be empty for iOS builds." FORCE)
execute_process(
  COMMAND xcodebuild -version -sdk iphonesimulator Path
  RESULT_VARIABLE result_process
  OUTPUT_VARIABLE DARWIN_iossim_SYSROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_FILE /dev/null
)
if(NOT result_process EQUAL 0)
  message(ERROR "Can't find ios simulator sdk")
endif()
set(CMAKE_OSX_SYSROOT ${DARWIN_iossim_SYSROOT})
add_tapi_test_library(Simulator
  FRAMEWORK
  PUBLIC_HEADERS Simulator.h
  SOURCE Simulator.c
)
