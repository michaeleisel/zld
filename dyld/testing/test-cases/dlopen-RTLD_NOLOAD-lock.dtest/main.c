
// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/libfoo.dylib -install_name $RUN_DIR/liblock.dylib
// BUILD:  $CC main.c -o $BUILD_DIR/dlopen-RTLD_NOLOAD-lock.exe  -DRUN_DIR="$RUN_DIR"

// RUN:  ./dlopen-RTLD_NOLOAD-lock.exe


//
// We are testing that dlopen(RTLD_NOLOAD) does not take the api lock
//

#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <mach-o/dyld_priv.h>

#include "test_support.h"



int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    void* h = dlopen(RUN_DIR "/libfoo.dylib", 0);
    if ( h == NULL )
        FAIL("dlopen(\"%s/libfoo.dylib\", 0) returned NULL", RUN_DIR);

    PASS("Success");
}

