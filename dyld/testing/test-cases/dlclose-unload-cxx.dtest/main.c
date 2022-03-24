
// BUILD:  $CC foo.c  -dynamiclib -install_name $RUN_DIR/libfoo.dylib  -o $BUILD_DIR/libfoo.dylib
// BUILD:  $CC bar.c  -dynamiclib -install_name $RUN_DIR/libbar.dylib  -o $BUILD_DIR/libbar.dylib
// BUILD:  $CC main.c -o $BUILD_DIR/dlclose-unload-cxx.exe -DRUN_DIR="$RUN_DIR"

// RUN:  ./dlclose-unload-cxx.exe

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dlfcn.h>

#include "test_support.h"

///
/// This tests that if a C++ symbol (any weak symbol) is bound to an image
/// that is dynamically unloaed, the image is not unloaded until all its clients are
///

typedef void* (*proc)(void);

bool inImage(void* x)
{
  Dl_info info;
  return ( dladdr(x, &info) != 0 );
}


int main()
{
    void* handle1 = dlopen(RUN_DIR "/libfoo.dylib", RTLD_LAZY);
    if ( handle1 == NULL ) {
        FAIL("dlclose-unload-c++: dlopen(\"libfoo.dylib\", RTLD_LAZY) failed with dlerror()=%s", dlerror());
    }

    proc fooProc = (proc)dlsym(handle1, "foo");
    if ( fooProc == NULL ) {
        FAIL("dlclose-unload-c++: dlsym(handle1, \"foo\") failed");
    }

    void* handle2 = dlopen(RUN_DIR "/libbar.dylib", RTLD_LAZY);
    if ( handle2 == NULL ) {
        FAIL("dlclose-unload-c++: dlopen(\"libfoo.dylib\", RTLD_LAZY) failed with dlerror()=%s", dlerror());
    }

    proc barProc = (proc)dlsym(handle2, "bar");
    if ( barProc == NULL ) {
        FAIL("dlclose-unload-c++: dlsym(handle2, \"bar\") failed");
    }

    // verify that uniquing is happening
    void* fooResult = (*fooProc)();
    void* barResult = (*barProc)();
    if ( fooResult != barResult ) {
        FAIL("dlclose-unload-c++: foo() and bar() returned different values");
    }

    // close libfoo, even though libbar is using libfoo
    dlclose(handle1);

    // error if libfoo was unloaded
    if ( !inImage(fooProc) ) {
        FAIL("dlclose-unload-c++: libfoo should not have been unloaded");
    }

    // close libbar which should release libfoo
    dlclose(handle2);

    // error if libfoo was not unloaded
    if ( inImage(fooProc) ) {
        FAIL("dlclose-unload-c++: libfoo should have been unloaded");
    }

    PASS("dlclose-unload-c++");
}
