// BUILD:  $CC bar.c -dynamiclib -o $BUILD_DIR/libbar.dylib -install_name $RUN_DIR/libbar.dylib
// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/libfoo.dylib -install_name $RUN_DIR/libfoo.dylib $BUILD_DIR/libbar.dylib -Wl,-no_fixup_chains
// BUILD:  $CC main.c -o $BUILD_DIR/lazy-weak-import.exe -DRUN_DIR="$RUN_DIR"

// BUILD: $SKIP_INSTALL $BUILD_DIR/libbar.dylib

// RUN:    ./lazy-weak-import.exe


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "test_support.h"


// Test that a lazy bind can be a weak-import and missing

int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    // to test in dlopen (instead of at launch) to make error handling easier
    void* handle = dlopen(RUN_DIR "/libfoo.dylib", RTLD_NOW);
    if ( handle == NULL ) {
        FAIL("dlopen(\"%s\") failed with: %s", RUN_DIR "/libfoo.dylib", dlerror());
        return 0;
    }

    PASS("Success");
    return 0;
}
