
// BUILD:  $CC other.c -o $BUILD_DIR/test.exe
// BUILD:  $CC main.c  -o $BUILD_DIR/dlopen-main-exe.exe -DRUN_DIR="$RUN_DIR"

// RUN:  ./dlopen-main-exe.exe


#include <stdbool.h>
#include <stdio.h>
#include <dlfcn.h>
#include <TargetConditionals.h>

#include "test_support.h"


int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    void* handle = dlopen(RUN_DIR "/test.exe", RTLD_LAZY);
#if TARGET_OS_OSX
    if ( handle != NULL )
        PASS("Success");
    else
        FAIL("dlopen-main-exe: dlopen() of a main executable should have worked on macOS");
#else
    if ( handle == NULL )
        PASS("Success");
    else
        FAIL("dlopen-main-exe: dlopen() of a main executable should have failed");
#endif
}


