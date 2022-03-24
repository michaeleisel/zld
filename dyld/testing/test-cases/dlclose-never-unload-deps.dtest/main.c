
// BUILD:  $CC bar.c -dynamiclib           -install_name $RUN_DIR/libbar.dylib -o $BUILD_DIR/libbar.dylib
// BUILD:  $CC foo.m -dynamiclib           -install_name $RUN_DIR/libfoo.dylib -o $BUILD_DIR/libfoo.dylib $BUILD_DIR/libbar.dylib
// BUILD:  $CC baz.c -dynamiclib           -install_name $RUN_DIR/libbaz.dylib -o $BUILD_DIR/libbaz.dylib
// BUILD:  $CC main.c -DRUN_DIR="$RUN_DIR"                                     -o $BUILD_DIR/dlclose-never-unload-deps.exe

// RUN:  ./dlclose-never-unload-deps.exe

// Make sure that dependents of never unload binaries are also never unloaded

#include <stdio.h>
#include <dlfcn.h>

#include "test_support.h"

extern int foo();
typedef __typeof(&foo) fooPtr;

int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    void* handle = dlopen(RUN_DIR "/libfoo.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlopen(\"libfoo.dylib\"), dlerror()=%s", dlerror());
    }

    fooPtr sym = (fooPtr)dlsym(handle, "foo");
    if ( sym == NULL ) {
        FAIL("dlsym(\"foo\") returned NULL, dlerror()=%s", dlerror());
    }

    if ( sym() != 42 ) {
        FAIL("Expected 42 on the first call to foo()");
    }

    int result = dlclose(handle);
    if ( result != 0 ) {
        FAIL("dlclose(handle) returned %d, dlerror()=%s", result, dlerror());
    }

    // Open and close baz.  This should have not cause bar.dylib to unload
    void* handle2 = dlopen(RUN_DIR "/libbaz.dylib", RTLD_LAZY);
    if ( handle2 == NULL ) {
        FAIL("dlopen(\"libbaz.dylib\"), dlerror()=%s", dlerror());
    }

    int result2 = dlclose(handle2);
    if ( result2 != 0 ) {
        FAIL("dlclose(handle2) returned %d, dlerror()=%s", result2, dlerror());
    }

    // Call foo()->bar() again.  It should not fail.
    if ( sym() != 42 ) {
        FAIL("Expected 42 on the second call to foo()");
    }

    PASS("Success");
}

