// BOOT_ARGS: dyld_flags=2

// BUILD(macos):  $CC my.c -dynamiclib -o $BUILD_DIR/libmy.dylib -install_name librelative.dylib
// BUILD(macos):  $CC main.c -o $BUILD_DIR/amfi-hardened-dlopen-relative.exe -DRUN_DIR="$RUN_DIR"

// BUILD(ios,tvos,watchos,bridgeos):

// RUN:  DYLD_AMFI_FAKE=0x14 ./amfi-hardened-dlopen-relative.exe
// RUN:  DYLD_AMFI_FAKE=0x3F ./amfi-hardened-dlopen-relative.exe


#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <dlfcn.h>
#include <mach/host_info.h>
#include <mach/mach.h>
#include <mach/mach_host.h>

#include "test_support.h"

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {

    // dlopen with an absolute path.  This should always succeed
    void* handle1 = dlopen(RUN_DIR "/libmy.dylib", RTLD_LAZY);
    if ( handle1 == NULL ) {
        FAIL("dlopen(%s) unexpectedly failed because: %s", RUN_DIR "/libmy.dylib", dlerror());
    }

    // dlopen with the install name.  This should always succeed, even though it looke like a file system relative path
    void* handle2 = dlopen("librelative.dylib", RTLD_LAZY);
    if ( handle2 == NULL ) {
        FAIL("dlopen(%s) unexpectedly failed because: %s", "librelative.dylib", dlerror());
    }

    PASS("Succcess");
}



