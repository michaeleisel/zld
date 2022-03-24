

// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/libfoo.dylib -install_name $RUN_DIR/libfoo.dylib
// BUILD:  $CC main.c -o $BUILD_DIR/dlsym-in-init.exe -DRUN_DIR="$RUN_DIR"

// RUN:  ./dlsym-in-init.exe

#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>

#include "test_support.h"


int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {

    void* handle = dlopen(RUN_DIR "/libfoo.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlopen(libfoo.dylib): %s", dlerror());
    }

    PASS("Success");
}

