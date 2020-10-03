add_custom_target(TapiUnitTests)
set_target_properties(TapiUnitTests PROPERTIES FOLDER "Tapi Tests")

function(add_tapi_unittest test_dirname)
  add_unittest(TapiUnitTests ${test_dirname} ${ARGN})
endfunction()

add_subdirectory(TapiCore)
add_subdirectory(TapiDriver)
add_subdirectory(TapiVerifier)
add_subdirectory(libtapi)
add_subdirectory(JSON)
