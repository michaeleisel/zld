
// BUILD:  $CC e.c -dynamiclib           -install_name $RUN_DIR/libe.dylib -o $BUILD_DIR/libe.dylib
// BUILD:  $CC d.c -dynamiclib           -install_name $RUN_DIR/libd.dylib -o $BUILD_DIR/libd.dylib
// BUILD:  $CC c.c -dynamiclib           -install_name $RUN_DIR/libc.dylib -o $BUILD_DIR/libc.dylib $BUILD_DIR/libd.dylib
// BUILD:  $CC b.c -dynamiclib           -install_name $RUN_DIR/libb.dylib -o $BUILD_DIR/libb.dylib $BUILD_DIR/libc.dylib
// BUILD:  $CC a.c -dynamiclib           -install_name $RUN_DIR/liba.dylib -o $BUILD_DIR/liba.dylib $BUILD_DIR/libb.dylib
// BUILD:  $CC main.c -DRUN_DIR="$RUN_DIR"                                 -o $BUILD_DIR/dlclose-never-unload-deps3.exe

// RUN:  ./dlclose-never-unload-deps3.exe

// Make sure that dependents of never unload binaries are also never unloaded

#include <stdio.h>
#include <dlfcn.h>

#include "test_support.h"

extern int aa();
typedef __typeof(&aa) aaPtr;

int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    void* handle = dlopen(RUN_DIR "/liba.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlopen(\"liba.dylib\"), dlerror()=%s", dlerror());
    }

    aaPtr sym = (aaPtr)dlsym(handle, "aa");
    if ( sym == NULL ) {
        FAIL("dlsym(\"aa\") returned NULL, dlerror()=%s", dlerror());
    }

    if ( sym() != 42 ) {
        FAIL("Expected 42 on the first call to aa()");
    }

    // Open and close libe.dylib to trigger a garbage collection
    void* handle2 = dlopen(RUN_DIR "/libe.dylib", RTLD_LAZY);
    if ( handle2 == NULL ) {
        FAIL("dlopen(\"libSystem.dylib\"), dlerror()=%s", dlerror());
    }

    int result2 = dlclose(handle2);
    if ( result2 != 0 ) {
        FAIL("dlclose(handle2) returned %d, dlerror()=%s", result2, dlerror());
    }

    // Call aa()->bb()->cc() again.  It should not fail.
    if ( sym() != 42 ) {
        FAIL("Expected 42 on the second call to aa()");
    }

    PASS("Success");
}

