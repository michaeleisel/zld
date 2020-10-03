set(files
  iOS.conf
  macOS.conf
  tvOS.conf
  watchOS.conf
  EquivalentTypes.conf
  )

set(output_dir ${CMAKE_BINARY_DIR}/local/tapi/config)

set(out_files)
foreach( f ${files} )
  set( src ${CMAKE_CURRENT_SOURCE_DIR}/${f} )
  set( dst ${output_dir}/${f} )
  add_custom_command(OUTPUT ${dst}
    DEPENDS ${src}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src} ${dst}
    COMMENT "Copying tapi configuraion ${f}...")
  list(APPEND out_files ${dst})
endforeach( f )

add_custom_target(tapi-configs ALL DEPENDS ${out_files})

install(
  FILES ${files}
  COMPONENT tapi-configs
  PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
  DESTINATION local/tapi/config)

if (NOT CMAKE_CONFIGURATION_TYPES) # don't add this for IDE's.
  add_llvm_install_targets(install-tapi-configs
                           DEPENDS tapi-configs
                           COMPONENT tapi-configs)
endif()
