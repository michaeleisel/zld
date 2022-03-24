
// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/fallback/libfoo.dylib -install_name $RUN_DIR/libfoo.dylib
// BUILD:  $CC main.c            -o $BUILD_DIR/dlopen-DYLD_FALLBACK_LIBRARY_PATH.exe
// BUILD:  $DYLD_ENV_VARS_ENABLE $BUILD_DIR/dlopen-DYLD_FALLBACK_LIBRARY_PATH.exe

// RUN:  DYLD_FALLBACK_LIBRARY_PATH=$RUN_DIR/fallback/ ./dlopen-DYLD_FALLBACK_LIBRARY_PATH.exe

#include <stdio.h>
#include <dlfcn.h>

#include "test_support.h"

extern int foo();

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
    const char* needPath = getenv("DYLD_FALLBACK_LIBRARY_PATH");
    if ( needPath == NULL ) {
        FAIL("DYLD_FALLBACK_LIBRARY_PATH not set");
    }

    // <rdar://5951327> (DYLD_FALLBACK_LIBRARY_PATH should only apply to dlopen() of leaf names)
    void* handle = dlopen("/nope/libfoo.dylib", RTLD_LAZY);
    if ( handle != NULL ) {
        FAIL("DYLD_FALLBACK_LIBRARY_PATH should be used only when calling dlopen with leaf names");
    }

    handle = dlopen("libfoo.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlerror(\"libfoo.dylib\"): %s", dlerror());
    }

    int ret = dlclose(handle);
    if ( ret != 0 ) {
        FAIL("dlclose() returned %d", ret);
    }

    PASS("Success");
}

