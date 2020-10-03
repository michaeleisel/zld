add_tapi_test_library(InstallName
  FRAMEWORK
  SOURCE InstallName.c
)
add_custom_command(TARGET InstallName
  POST_BUILD
  COMMAND install_name_tool -id /System/Library/Frameworks/InstallName.framework/Versions/A/Frameworks/libInstallName.dylib ${TAPI_BINARY_DIR}/Inputs/System/Library/Frameworks/InstallName.framework/Versions/A/InstallName
)
