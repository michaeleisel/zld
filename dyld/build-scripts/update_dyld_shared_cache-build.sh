set -e # exit when any command fails
set -v # verbose

if [ "${RC_PURPLE}" = "" ]
then
    # macOS platform
    OBJROOT_BDR="${TARGET_TEMP_DIR}/Objects_builder"
    xcodebuild ${ACTION} -target dyld_shared_cache_builder     OBJROOT="${OBJROOT_BDR}" SDKROOT="${SDKROOT}" MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET} SRCROOT="${SRCROOT}" DSTROOT="${DSTROOT}" SYMROOT="${SYMROOT}" RC_ProjectSourceVersion="${RC_ProjectSourceVersion}"  DISABLE_SDK_METADATA_PARSING=YES  RC_PLATFORM_INSTALL_PATH=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform

    OBJROOT_MAC="${TARGET_TEMP_DIR}/Objects_mac"
    xcodebuild ${ACTION} -target update_dyld_shared_cache_tool OBJROOT="${OBJROOT_MAC}" SDKROOT="${SDKROOT}" MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET} SRCROOT="${SRCROOT}" DSTROOT="${DSTROOT}" SYMROOT="${SYMROOT}" RC_ProjectSourceVersion="${RC_ProjectSourceVersion}" DISABLE_SDK_METADATA_PARSING=YES

    OBJROOT_SLC="${TARGET_TEMP_DIR}/Objects_slc"
    xcodebuild ${ACTION} -target libslc_builder.dylib          OBJROOT="${OBJROOT_SLC}" SDKROOT="${SDKROOT}" MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET} SRCROOT="${SRCROOT}" DSTROOT="${DSTROOT}" SYMROOT="${SYMROOT}" RC_ProjectSourceVersion="${RC_ProjectSourceVersion}" DISABLE_SDK_METADATA_PARSING=YES

    # Rosetta needs the dsc_extractor on macOS.  They extract the cache of the host OS.
    OBJROOT_XTR="${TARGET_TEMP_DIR}/Objects_extractor"
    xcodebuild ${ACTION} -target dsc_extractor             OBJROOT="${OBJROOT_XTR}" SDKROOT="${SDKROOT}" MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET} SRCROOT="${SRCROOT}" DSTROOT="${DSTROOT}" SYMROOT="${SYMROOT}" RC_ProjectSourceVersion="${RC_ProjectSourceVersion}"  DISABLE_SDK_METADATA_PARSING=YES

    # build the kernel linker twice, once rpath based in toolchain and once in /usr/lib/
    if [ "${ACTION}" != "installhdrs" ]
    then
        OBJROOT_KBT="${TARGET_TEMP_DIR}/Objects_kcb_macToolchain"
        xcodebuild ${ACTION} -target libKernelCollectionBuilder OBJROOT="${OBJROOT_KBT}" LD_DYLIB_INSTALL_NAME="@rpath/libKernelCollectionBuilder.dylib"   INSTALL_PATH="${DT_TOOLCHAIN_DIR}/usr/lib/" SYMROOT="${SYMROOT}"     SDKROOT="${SDKROOT}" MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET} SRCROOT="${SRCROOT}" DSTROOT="${DSTROOT}" RC_ProjectSourceVersion="${RC_ProjectSourceVersion}"
        OBJROOT_KBO="${TARGET_TEMP_DIR}/Objects_kb_os"
        xcodebuild ${ACTION} -target libKernelCollectionBuilder OBJROOT="${OBJROOT_KBO}" LD_DYLIB_INSTALL_NAME="/usr/lib/libKernelCollectionBuilder.dylib" INSTALL_PATH="/usr/lib"                     SYMROOT="${SYMROOT}/os"  SDKROOT="${SDKROOT}" MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET} SRCROOT="${SRCROOT}" DSTROOT="${DSTROOT}" RC_ProjectSourceVersion="${RC_ProjectSourceVersion}"
    fi

    # move build results to host locations
    if [ "${ACTION}" == "install" ]
    then
        #mkdir -p "${DSTROOT}/${DEVELOPER_INSTALL_DIR}/Platforms/MacOSX.platform/usr/local"
        echo mkdir -p "${DSTROOT}/${DEVELOPER_INSTALL_DIR}/Platforms/MacOSX.platform/usr/local"
        echo mv "${DSTROOT}/usr/local/bin" "${DSTROOT}/${DEVELOPER_INSTALL_DIR}/Platforms/MacOSX.platform/usr/local/"
        #mv "${DSTROOT}/usr/local/bin" "${DSTROOT}/${DEVELOPER_INSTALL_DIR}/Platforms/MacOSX.platform/usr/local/"
    fi

    # copy performance files from SDK to platform
    if [ -r "${SDKROOT}/AppleInternal/DirtyDataFiles/dirty-data-segments-order.txt" ]; then
        mkdir -p "${DSTROOT}/usr/local/bin"
        cp "${SDKROOT}/AppleInternal/DirtyDataFiles/dirty-data-segments-order.txt"  "${DSTROOT}/usr/local/bin"
    fi
    if [ -r "${SDKROOT}/AppleInternal/OrderFiles/dylib-order.txt" ]; then
        mkdir -p "${DSTROOT}/usr/local/bin"
        cp "${SDKROOT}/AppleInternal/OrderFiles/dylib-order.txt"  "${DSTROOT}/usr/local/bin"
    fi

else
    # for iOS/tvOS/watchOS/bridgeOS platform, build "host" tools
    OBJROOT_BDR="${TARGET_TEMP_DIR}/Objects_builder"
    xcodebuild ${ACTION} -target dyld_shared_cache_builder OBJROOT="${OBJROOT_BDR}" SDKROOT="${SDKROOT}" MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET} SRCROOT="${SRCROOT}" DSTROOT="${DSTROOT}" SYMROOT="${SYMROOT}" RC_ProjectSourceVersion="${RC_ProjectSourceVersion}"  DISABLE_SDK_METADATA_PARSING=YES

    OBJROOT_SLC="${TARGET_TEMP_DIR}/Objects_slc"
    xcodebuild ${ACTION} -target libslc_builder.dylib      OBJROOT="${OBJROOT_SLC}" SDKROOT="${SDKROOT}" MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET} SRCROOT="${SRCROOT}" DSTROOT="${DSTROOT}" SYMROOT="${SYMROOT}" RC_ProjectSourceVersion="${RC_ProjectSourceVersion}" DISABLE_SDK_METADATA_PARSING=YES

    OBJROOT_UTL="${TARGET_TEMP_DIR}/Objects_utils"
    xcodebuild ${ACTION} -target dyld_shared_cache_util    OBJROOT="${OBJROOT_UTL}" SDKROOT="${SDKROOT}" MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET}  SRCROOT="${SRCROOT}" DSTROOT="${DSTROOT}" SYMROOT="${SYMROOT}" RC_ProjectSourceVersion="${RC_ProjectSourceVersion}"  DISABLE_SDK_METADATA_PARSING=YES

    OBJROOT_DSC="${TARGET_TEMP_DIR}/Objects_libdsc"
    xcodebuild ${ACTION} -target libdsc                    OBJROOT="${OBJROOT_DSC}" SDKROOT="${SDKROOT}" MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET}  SRCROOT="${SRCROOT}" DSTROOT="${DSTROOT}" SYMROOT="${SYMROOT}" RC_ProjectSourceVersion="${RC_ProjectSourceVersion}"  DISABLE_SDK_METADATA_PARSING=YES

    OBJROOT_XTR="${TARGET_TEMP_DIR}/Objects_extractor"
    xcodebuild ${ACTION} -target dsc_extractor             OBJROOT="${OBJROOT_XTR}" SDKROOT="${SDKROOT}" MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET} SRCROOT="${SRCROOT}" DSTROOT="${DSTROOT}" SYMROOT="${SYMROOT}" RC_ProjectSourceVersion="${RC_ProjectSourceVersion}"  DISABLE_SDK_METADATA_PARSING=YES

    TC=$(basename $TOOLCHAIN_DIR)
    CANON_TOOLCHAIN_DIR="/Applications/Xcode.app/Contents/Developer/Toolchains/${TC}"

    OBJROOT_KBT="${TARGET_TEMP_DIR}/Objects_kcb"
    xcodebuild ${ACTION} -target libKernelCollectionBuilder OBJROOT="${OBJROOT_KBT}" LD_DYLIB_INSTALL_NAME="@rpath/libKernelCollectionBuilder.dylib"   INSTALL_PATH="${CANON_TOOLCHAIN_DIR}/usr/lib/" SYMROOT="${SYMROOT}"     SDKROOT="${SDKROOT}" MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET} SRCROOT="${SRCROOT}" DSTROOT="${DSTROOT}" RC_ProjectSourceVersion="${RC_ProjectSourceVersion}"

    # no simulator for bridgeOS
    if [ "${RC_BRIDGE}" != "YES" ]
    then
        OBJROOT_SIM="${TARGET_TEMP_DIR}/Objects_Sim"
        xcodebuild ${ACTION} -target update_dyld_sim_shared_cache OBJROOT="${OBJROOT_SIM}" SDKROOT="${SDKROOT}" MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET} SRCROOT="${SRCROOT}" DSTROOT="${DSTROOT}" SYMROOT="${SYMROOT}" RC_ProjectSourceVersion="${RC_ProjectSourceVersion}" DISABLE_SDK_METADATA_PARSING=YES
    fi

    # move roots to platform dir
    if [ -e ${DSTROOT}/usr/local/include ]
    then
        mkdir -p "${DSTROOT}/${RC_PLATFORM_INSTALL_PATH}/"
        cp -R "${DSTROOT}/usr" "${DSTROOT}/${RC_PLATFORM_INSTALL_PATH}/"
        rm -r "${DSTROOT}/usr"
    fi

    # copy performance files from SDK to platform
    if [ -r "${ARM_SDK}/AppleInternal/DirtyDataFiles/dirty-data-segments-order.txt" ];
    then
        mkdir -p "${DSTROOT}/${RC_PLATFORM_INSTALL_PATH}/usr/local/bin"
        cp "${ARM_SDK}/AppleInternal/DirtyDataFiles/dirty-data-segments-order.txt"  "${DSTROOT}/${RC_PLATFORM_INSTALL_PATH}/usr/local/bin"
    fi
    if [ -r "${ARM_SDK}/AppleInternal/OrderFiles/dylib-order.txt" ];
    then
        mkdir -p "${DSTROOT}/${RC_PLATFORM_INSTALL_PATH}/usr/local/bin"
        cp "${ARM_SDK}/AppleInternal/OrderFiles/dylib-order.txt"  "${DSTROOT}/${RC_PLATFORM_INSTALL_PATH}/usr/local/bin"
    fi

fi

