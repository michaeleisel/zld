

// BUILD:  $CC foo.c -DRUN_DIR="$RUN_DIR" -dynamiclib -o $BUILD_DIR/libfoo.dylib -install_name $RUN_DIR/libfoo.dylib
// BUILD:  $CC main.c -DRUN_DIR="$RUN_DIR" -o $BUILD_DIR/dlclose-in-init.exe

// RUN:  ./dlclose-in-init.exe

#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <mach-o/dyld_priv.h>

#include "test_support.h"


int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
    void* handle = dlopen(RUN_DIR "/libfoo.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlopen(libfoo.dylib) expected to pass");
    }

    // iterate loaded images and make sure no residue from failed dlopen
    const char* foundPath = NULL;
    int count = _dyld_image_count();
    bool found = false;
    for (int i=0; i < count; ++i) {
        const char* path = _dyld_get_image_name(i);
        //LOG("path[%2d]=%s", i, path);
        if ( strcmp(path, RUN_DIR "/libfoo.dylib") == 0 ) {
            found = true;
            break;
        }
    }

    if (!found)
		FAIL("Failed to find libfoo.dylib in loaded image list");
    else
		PASS("Success");
}

