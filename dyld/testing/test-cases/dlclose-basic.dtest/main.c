
// BUILD:  $CC foo.c -bundle               -o $BUILD_DIR/test.bundle
// BUILD:  $CC main.c -DRUN_DIR="$RUN_DIR" -o $BUILD_DIR/dlclose-basic.exe

// RUN:  ./dlclose-basic.exe

#include <stdio.h>
#include <dlfcn.h>

#include "test_support.h"

int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    void* handle = dlopen(RUN_DIR "/test.bundle", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlopen(\"test.bundle\"), dlerror()=%s", dlerror());
    }

    void* sym = dlsym(handle, "foo");
    if ( sym == NULL ) {
        FAIL("dlsym(\"foo\") for \"test.bundle\" returned NULL, dlerror()=%s", dlerror());
    }

    int result = dlclose(handle);
    if ( result != 0 ) {
        FAIL("dlclose(handle) returned %d, dlerror()=%s", result, dlerror());
    }

    // close a second time and verify it fails
    int result2 = dlclose(handle);
    if ( result2 == 0 ) {
        FAIL("second dlclose() unexpectedly returned 0");
    }


    // try some bad handles
    void* badHandle = "hi there";
    int result3 = dlclose(badHandle);
    if ( result3 == 0 ) {
        FAIL("dlclose(badHandle) unexpectedly returned 0");
    }
    result3 = dlclose((void*)0x12345678);
    if ( result3 == 0 ) {
        FAIL("dlclose(0x12345678) unexpectedly returned 0");
    }

    // open and close something from dyld cache
    void* handle4 = dlopen("/usr/lib/libSystem.B.dylib", RTLD_LAZY);
    if ( handle4 == NULL ) {
        FAIL("dlopen(\"/usr/lib/libSystem.B.dylib\"), dlerror()=%s", dlerror());
    }
    int result4 = dlclose(handle4);
    if ( result4 != 0 ) {
        FAIL("second dlclose() returned %d: %s", result4, dlerror());
    }

    PASS("Success");
}

