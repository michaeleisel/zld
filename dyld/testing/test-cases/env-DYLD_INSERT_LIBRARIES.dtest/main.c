
// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/libfoo.dylib -install_name $RUN_DIR/libfoo.dylib
// BUILD:  $CC main.c            -o $BUILD_DIR/env-DYLD_LIBRARY_PATH.exe
// BUILD:  $DYLD_ENV_VARS_ENABLE $BUILD_DIR/env-DYLD_LIBRARY_PATH.exe

// RUN:  DYLD_INSERT_LIBRARIES=$RUN_DIR/libfoo.dylib              ./env-DYLD_LIBRARY_PATH.exe
// RUN:  DYLD_INSERT_LIBRARIES=/usr/lib/swift/libswiftCore.dylib  ./env-DYLD_LIBRARY_PATH.exe
// RUN:  DYLD_INSERT_LIBRARIES=/usr/lib/libSystem.B.dylib         ./env-DYLD_LIBRARY_PATH.exe

// verifies three cases of what can be inserted:
//  1) standalone dylib on disk
//  2) a dylib in the dyld cache that would not have been loaded
//  3) a dylib in the dyld cache that is already loaded


#include <stdio.h>
#include <stdlib.h>
#include <mach-o/dyld.h>

#include "test_support.h"

extern int foo();

int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    const char* needPath = getenv("DYLD_INSERT_LIBRARIES");
    if ( needPath == NULL ) {
        FAIL("DYLD_INSERT_LIBRARIES not set");
        return 0;
    }

    bool found = false;
    int count = _dyld_image_count();
    for (int i=0; i < count; ++i) {
        const char* imagePath = _dyld_get_image_name(i);
        //printf("image[%d]=%s\n", i, imagePath);
        if ( imagePath && (strcmp(imagePath, needPath) == 0) ) {
            PASS("Found inserted dylib");
            return 0;
        }
    }

    FAIL("dylib not inserted");
    return 0;
}


