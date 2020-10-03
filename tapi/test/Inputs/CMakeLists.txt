# Reset flags
set(CMAKE_C_FLAGS "")
set(CMAKE_CXX_FLAGS "")
# Force release build
set(CMAKE_BUILD_TYPE "Release")
# Disable LTO for test input
set(LLVM_ENABLE_LTO OFF)
add_custom_target(TapiTestInputs)

function(add_tapi_test_library name)
  cmake_parse_arguments(ARG
    "FRAMEWORK"
    "CURRENT_VERSION;INSTALL_NAME_DIR;OUTPUT_NAME;OUTPUT_DIRECTORY;OUTPUT_DIRECTORY_PREFIX;OUTPUT_DIRECTORY_SUFFIX"
    "PUBLIC_HEADERS;PRIVATE_HEADERS;SOURCE;ARCHITECTURES"
    ${ARGN}
  )

  if (NOT DEFINED ARG_CURRENT_VERSION)
    set(ARG_CURRENT_VERSION 1)
  endif()
  if (NOT DEFINED ARG_INSTALL_NAME_DIR)
    if (${ARG_FRAMEWORK})
      set(ARG_INSTALL_NAME_DIR /System/Library/Frameworks)
    else()
      set(ARG_INSTALL_NAME_DIR /usr/lib)
    endif()
  endif()
  set (LIBRARY_OUTPUT_DIRECTORY "${TAPI_BINARY_DIR}/Inputs/")
  if (DEFINED ARG_OUTPUT_DIRECTORY)
    string(APPEND LIBRARY_OUTPUT_DIRECTORY "${ARG_OUTPUT_DIRECTORY}/")
  else()
    if (DEFINED ARG_OUTPUT_DIRECTORY_PREFIX)
      string(APPEND LIBRARY_OUTPUT_DIRECTORY "${ARG_OUTPUT_DIRECTORY_PREFIX}/")
    endif()
    if (${ARG_FRAMEWORK})
      string(APPEND LIBRARY_OUTPUT_DIRECTORY "System/Library/Frameworks/")
    else()
      string(APPEND LIBRARY_OUTPUT_DIRECTORY "usr/lib/")
    endif()
    if (DEFINED ARG_OUTPUT_DIRECTORY_SUFFIX)
      string(APPEND LIBRARY_OUTPUT_DIRECTORY "${ARG_OUTPUT_DIRECTORY_SUFFIX}/")
    endif()
  endif()

  add_library(${name}
    SHARED
    EXCLUDE_FROM_ALL
    ${ARG_PUBLIC_HEADERS}
    ${ARG_PRIVATE_HEADERS}
    ${ARG_SOURCE}
  )

  if(${ARG_FRAMEWORK})
    set_target_properties(${name}
      PROPERTIES
      FRAMEWORK TRUE
      FRAMEWORK_VERSION A
    )
  endif()

  set_target_properties(${name}
    PROPERTIES
    PUBLIC_HEADER "${ARG_PUBLIC_HEADERS}"
    PRIVATE_HEADER "${ARG_PRIVATE_HEADERS}"
    VERSION ${ARG_CURRENT_VERSION}
    SOVERSION 1
    INSTALL_NAME_DIR ${ARG_INSTALL_NAME_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${LIBRARY_OUTPUT_DIRECTORY}
  )

  if(DEFINED ARG_ARCHITECTURES)
    set_target_properties(${name}
      PROPERTIES
      OSX_ARCHITECTURES "${ARG_ARCHITECTURES}"
      C_COMPILER_LAUNCHER ""
      CXX_COMPILER_LAUNCHER ""
    )
  endif()

  if(DEFINED ARG_OUTPUT_NAME)
    set_target_properties(${name}
      PROPERTIES
      OUTPUT_NAME ${ARG_OUTPUT_NAME}
    )
  endif()

  if(NOT ${ARG_FRAMEWORK})
    if(DEFINED ARG_PUBLIC_HEADERS)
      #file(MAKE_DIRECTORY ${TAPI_BINARY_DIR}/Inputs/usr/include)
      file(COPY ${ARG_PUBLIC_HEADERS} DESTINATION ${LIBRARY_OUTPUT_DIRECTORY}/../include)
    endif()
    if(DEFINED ARG_PRIVATE_HEADERS)
      file(COPY ${ARG_PRIVATE_HEADERS} DESTINATION ${LIBRARY_OUTPUT_DIRECTORY}/../local/include)
    endif()
  endif()
  add_dependencies(TapiTestInputs ${name})
endfunction(add_tapi_test_library)

add_subdirectory(ABrokenSymlink)
add_subdirectory(AliasList)
add_subdirectory(ArchDiff)
add_subdirectory(asm)
add_subdirectory(AvailabilityTest)
add_subdirectory(Bundle)
add_subdirectory(CodeCoverage)
add_subdirectory(CPP1)
add_subdirectory(CPP2)
add_subdirectory(CPP3)
add_subdirectory(CPP4)
add_subdirectory(Dummy)
add_subdirectory(Export)
add_subdirectory(FooLib)
add_subdirectory(FragileObjCABI)
add_subdirectory(Inlining)
add_subdirectory(InstallName)
add_subdirectory(IVarTest)
add_subdirectory(IVarExtensionTest)
add_subdirectory(Mod)
add_subdirectory(Mod2)
add_subdirectory(Project)
add_subdirectory(Public)
add_subdirectory(Reexport)
add_subdirectory(ReexportAll)
add_subdirectory(Simple)
add_subdirectory(Simulator)
add_subdirectory(SpecialLinkerSymbols)
add_subdirectory(SubFramework)
add_subdirectory(SubFrameworks)
add_subdirectory(Swift)
add_subdirectory(SymlinkTest)
add_subdirectory(TapiDefine)
add_subdirectory(Umbrella1)
add_subdirectory(Umbrella2)
add_subdirectory(Umbrella3)
add_subdirectory(UmbrellaCC)
add_subdirectory(UsrLib)

if (LINKER_SUPPORTS_IOSMAC)
add_subdirectory(Zippered)
add_subdirectory(ZipperedAPI)
add_subdirectory(IOSMac)
endif()
