set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/tapi-import.exports)

add_tapi_executable(tapi-import
  tapi-import.cpp
  )

target_link_libraries(tapi-import
  tapiCore
  )

install(TARGETS tapi-import
  RUNTIME DESTINATION bin
  COMPONENT tapi-import
  )

add_llvm_install_targets(install-tapi-import
                         DEPENDS tapi-import
                       COMPONENT tapi-import)
