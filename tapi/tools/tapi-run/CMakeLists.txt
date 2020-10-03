set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/tapi-run.exports)

add_tapi_executable(tapi-run
  tapi-run.cpp
  )

target_link_libraries(tapi-run
  tapiCore
  libtapi
  )

set_property(TARGET tapi-run APPEND_STRING
  PROPERTY
  LINK_FLAGS " -client_name ld"
  )

install(TARGETS tapi-run
  RUNTIME DESTINATION bin
  COMPONENT tapi-run
  )
add_llvm_install_targets(install-tapi-run
                         DEPENDS tapi-run
                         COMPONENT tapi-run)
