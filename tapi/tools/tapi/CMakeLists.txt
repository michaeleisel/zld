install(
  DIRECTORY ${LLVM_LIBRARY_OUTPUT_INTDIR}/clang/${CLANG_VERSION}/include
  DESTINATION lib${LLVM_LIBDIR_SUFFIX}/tapi/${TAPI_VERSION}/
  COMPONENT tapi-clang-headers
  )

add_custom_target(tapi-clang-headers ALL DEPENDS clang-headers)
add_llvm_install_targets(install-tapi-clang-headers
                         DEPENDS tapi-clang-headers
                         COMPONENT tapi-clang-headers)

set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/tapi.exports)

add_tapi_executable(tapi
  tapi.cpp
  )

target_link_libraries(tapi
  tapiDriver
  )

if (TAPI_BUILD_LIBIOSSDK)
target_link_libraries(tapi
  iOSSDK
  )
endif()

add_dependencies(tapi tapi-clang-headers tapi-configs)

install(TARGETS tapi
  RUNTIME DESTINATION bin
  COMPONENT tapi
  )

add_llvm_install_targets(install-tapi
                         DEPENDS tapi install-tapi-clang-headers
                         COMPONENT tapi)
