add_tapi_unittest(TapiDriverTests
  HeaderGlob.cpp
  )

target_link_libraries(TapiDriverTests
  PRIVATE
  tapiDriver
  )
