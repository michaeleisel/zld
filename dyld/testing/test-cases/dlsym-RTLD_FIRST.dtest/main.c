
// BUILD:  $CC foo.c -dynamiclib  -install_name $RUN_DIR/libfoo.dylib -o $BUILD_DIR/libfoo.dylib
// BUILD:  $CC main.c -o $BUILD_DIR/dlsym-RTLD_FIRST.exe -DRUN_DIR="$RUN_DIR"

// RUN:  ./dlsym-RTLD_FIRST.exe

#include <stdio.h>
#include <dlfcn.h>
#include <string.h>
#include <mach-o/dyld_priv.h>

#include "test_support.h"

// verify RTLD_FIRST search order


static bool symbolInImage(const void* symAddr, const char* pathMatch)
{
    const char* imagePath = dyld_image_path_containing_address(symAddr);
    if ( imagePath == NULL )
        return false;
    return (strstr(imagePath, pathMatch) != NULL);
}




int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    // verify RTLD_FIRST only looks in immediate handle
    void* handle1 = dlopen(RUN_DIR "/libfoo.dylib", RTLD_FIRST);
    if ( handle1 == NULL ) {
        FAIL("dlerror(): %s", dlerror());
    }
    void* malloc1 = dlsym(handle1, "malloc");
    if ( malloc1 != NULL ) {
        FAIL("dlopen(RTLD_FIRST) did not hide malloc");
    }
    void* free1 = dlsym(handle1, "free");
    if ( free1 == NULL ) {
        FAIL("dlsym(handle1, \"free\") failed");
    }
    if ( !symbolInImage(free1, "libfoo.dylib") ) {
        FAIL("free should have been found in libfoo.dylib");
    }


    // verify not using RTLD_FIRST continues search and finds malloc in libSystem
    void* handle2 = dlopen(RUN_DIR "/libfoo.dylib", RTLD_LAZY);
    if ( handle2 == NULL ) {
        FAIL("dlerror(): %s", dlerror());
    }
    void* malloc2 = dlsym(handle2, "malloc");
    if ( malloc2 == NULL ) {
        FAIL("dlsym(handle2, \"malloc\") failed");
    }
    void* free2 = dlsym(handle2, "free");
    if ( free2 == NULL ) {
        FAIL("dlsym(handle2, \"free\") failed");
    }
    if ( !symbolInImage(free2, "libfoo.dylib") ) {
        FAIL("free should have been found in libfoo.dylib");
    }

    PASS("Success");
}

