# Test runner infrastructure for TAPI. This configures the TAPI test trees
# for use by Lit, and delegates to LLVM's lit test handlers.

add_subdirectory(Inputs)

if (CMAKE_CFG_INTDIR STREQUAL ".")
  set(LLVM_BUILD_MODE ".")
else ()
  set(LLVM_BUILD_MODE "%(build_mode)s")
endif ()

string(REPLACE ${CMAKE_CFG_INTDIR} ${LLVM_BUILD_MODE} TAPI_TOOLS_DIR ${LLVM_RUNTIME_OUTPUT_INTDIR})

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
  )

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/Unit/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/Unit/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/Unit/lit.cfg.py
  )

list(APPEND TAPI_TEST_DEPS
  tapi
  tapi-run
  clang-headers
  FileCheck
  hmaptool
  not
  TapiTestInputs
  yaml2obj
  tapi-frontend
  tapi-api-verifier
  tapi-binary-reader
  tapi-import
  tapi-configs
  )

set(TAPI_TEST_PARAMS
  tapi_site_config=${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  )

add_lit_testsuite(check-tapi "Running the TAPI regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  #LIT ${LLVM_LIT}
  PARAMS ${TAPI_TEST_PARAMS}
  DEPENDS ${TAPI_TEST_DEPS}
  ARGS ${TAPI_TEST_EXTRA_ARGS}
  )
set_target_properties(check-tapi PROPERTIES FOLDER "Tapi Tests")
