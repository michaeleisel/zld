set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/tapi-api-verifier.exports)

set(LLVM_LINK_COMPONENTS
  Support
  )

add_tapi_executable(tapi-api-verifier
  tapi-api-verifier.cpp
  )

target_link_libraries(tapi-api-verifier
  PRIVATE
  tapiAPIVerifier
  tapiCore
  tapiDriver
  tapiFrontend
  )

install(TARGETS tapi-api-verifier
  RUNTIME DESTINATION local/bin
  COMPONENT tapi-api-verifier
  )

add_llvm_install_targets(install-tapi-api-verifier
  DEPENDS tapi-api-verifier
  COMPONENT tapi-api-verifier
  )
