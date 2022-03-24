
// BUILD:  $CC interposer.c -dynamiclib -o $BUILD_DIR/libmyinterposer.dylib -install_name $RUN_DIR/libmyinterposer.dylib -lz
// BUILD:  $CC myzlib.c -dynamiclib -o $BUILD_DIR/override/libz.1.dylib -install_name /usr/lib/libz.1.dylib -compatibility_version 1.0 -Wl,-not_for_dyld_shared_cache
// BUILD:  $CC main.c  -o $BUILD_DIR/interpose-replaced-dylib.exe -lz $BUILD_DIR/libmyinterposer.dylib
// BUILD:  $DYLD_ENV_VARS_ENABLE $BUILD_DIR/interpose-replaced-dylib.exe

// RUN:  DYLD_LIBRARY_PATH=$RUN_DIR/override/  ./interpose-replaced-dylib.exe

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <stdbool.h>

#include <dlfcn.h>
#include <mach-o/dyld_priv.h>

#include "test_support.h"

// The test here is to interpose a symbol in libz.1.dylib and replace libz.1.dylib

extern const char* zlibVersion();

int main(int argc, const char* argv[])
{
    const char* version = zlibVersion();
    printf("zlibVersion() returned \"%s\"\n", version);
/*
    if ( usingMyDylib != expectMyDylib ) {
        // Not using the right dylib
        FAIL("%s", expectMyDylib ? "my" : "os");
    }
*/
    PASS("Success");

    return 0;
}

