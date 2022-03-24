
// BUILD:  $CC base.c -dynamiclib -o $BUILD_DIR/libbase.dylib -install_name $RUN_DIR/libbase.dylib
// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/libfoo.dylib -install_name $RUN_DIR/libfoo.dylib  $BUILD_DIR/libbase.dylib
// BUILD:  $CC main.c            -o $BUILD_DIR/env-DYLD_LIBRARY_PATH-init-order.exe  $BUILD_DIR/libbase.dylib
// BUILD:  $DYLD_ENV_VARS_ENABLE $BUILD_DIR/env-DYLD_LIBRARY_PATH-init-order.exe

// RUN:  DYLD_INSERT_LIBRARIES=$RUN_DIR/libfoo.dylib  ./env-DYLD_LIBRARY_PATH-init-order.exe


// verifies initializers in an inserted dylib run before initializers in the main executable


#include <stdio.h>
#include <stdlib.h>
#include <mach-o/dyld.h>

#include "test_support.h"

extern void mainInitCalled();
extern int getState();


__attribute__((constructor))
static void mainInit()
{
    mainInitCalled();
}

int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    if ( getState() == 3)
        PASS("success");
    else
        FAIL("initializer order wrong");
    return 0;
}


