
// BUILD:  $CC foo.c -dynamiclib -install_name @rpath/libimplicitrpath.dylib -o $BUILD_DIR/dir1/libimplicitrpath.dylib
// BUILD:  $CC foo.c -dynamiclib -install_name @rpath/libimplicitdeeprpath.dylib -o $BUILD_DIR/dir1/dir2/libimplicitdeeprpath.dylib
// BUILD:  $CC main.c -o $BUILD_DIR/dlopen-rpath-implicit.exe -rpath @loader_path/dir1

// RUN:  ./dlopen-rpath-implicit.exe

#include <stdio.h>
#include <dlfcn.h>

#include "test_support.h"

/// test that a leaf name passed to dlopen() searches the rpath

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
    void* handle = dlopen("libimplicitrpath.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlopen(\"libimplicitrpath.dylib\") failed: %s", dlerror());
    }

    // verify that implicit rpath works with more than just leaf names
    handle = dlopen("dir2/libimplicitdeeprpath.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlopen(\"dir2/libimplicitdeeprpath.dylib\") failed: %s", dlerror());
    }

    dlclose(handle);
    PASS("Succcess");
}

