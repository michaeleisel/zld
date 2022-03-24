
// BUILD:  $CC bar.c -dynamiclib -install_name @rpath/libbar.dylib -o $BUILD_DIR/dir1/libbar.dylib
// BUILD:  $CC foo.c -dynamiclib -install_name $RUN_DIR/libfoo.dylib -o $BUILD_DIR/libfoo.dylib $BUILD_DIR/dir1/libbar.dylib -rpath @loader_path/dir1/
// BUILD:  $CC main.c -o $BUILD_DIR/dlopen-rpath-implicit-loaded.exe $BUILD_DIR/libfoo.dylib

// RUN:  ./dlopen-rpath-implicit-loaded.exe

#include <stdio.h>
#include <dlfcn.h>

#include "test_support.h"

/// test that if there is no current LC_RPATH to find a dylib, if it is already loaded it will be found by dlopen()

int main()
{
    // at this point dir1/libbar.dylib is already loaded because libfoo.dylib linked with it
    // but there is no LC_RPATHs which can find libbar.dylib (and it is not in current dir)
    // so we are testing that implicit rpath also searches already loaded images.
    void* handle = dlopen("libbar.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlopen(\"libbar.dylib\") failed: %s", dlerror());
    }

    // verify explict use of @rpath also finds already loaded dylib
    handle = dlopen("@rpath/libbar.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlopen(\"@rpath/libbar.dylib\") failed: %s", dlerror());
    }

    PASS("Succcess");
}

