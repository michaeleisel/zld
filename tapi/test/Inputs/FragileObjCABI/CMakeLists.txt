if (SDK_SUPPORTS_I386)
  set(ARCHITECTURES i386 x86_64)
else()
  set(ARCHITECTURES x86_64)
endif()

add_tapi_test_library(FragileObjCABI
  FRAMEWORK
  PUBLIC_HEADERS FragileObjCABI.h
  SOURCE FragileObjCABI.m
  ARCHITECTURES ${ARCHITECTURES}
)
target_link_libraries(FragileObjCABI PRIVATE objc)
