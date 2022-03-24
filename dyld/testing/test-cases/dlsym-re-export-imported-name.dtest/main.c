
// BUILD:  $CC main.c  -o $BUILD_DIR/dlsym-reexport-imported-name.exe

// RUN: ./dlsym-reexport-imported-name.exe

// When searching for symbols, we try to avoid looking in the same dylibs multiple times
// However, libsystem_c re-exports memmove as platform_memmove from libsystem_platform.
// In this case, even if we have already searched in libsystem_platform for memmove, we want
// to look again for platform_memmove.

#include <stdio.h>
#include <dlfcn.h>

#include "test_support.h"

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
    void* handle = dlopen("/usr/lib/libSystem.B.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlerror(): %s", dlerror());
    }

    void* sym1 = dlsym(handle, "memmove");
    if ( sym1 == NULL ) {
        FAIL("dlerror(): %s", dlerror());
    }

    PASS("Success");
}

