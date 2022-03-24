
// BUILD:  $CC a.c -dynamiclib -o $BUILD_DIR/libgood.dylib -install_name $RUN_DIR/libgood.dylib
// BUILD:  $CC c.c -dynamiclib -o $BUILD_DIR/hide/libtestsymbol2extra.dylib -install_name $RUN_DIR/libtestsymbol2.dylib -DEXTRA_SYMBOL=1
// BUILD:  $CC c.c -dynamiclib -o $BUILD_DIR/libtestsymbol2.dylib      -install_name $RUN_DIR/libtestsymbol2.dylib
// BUILD:  $CC b.m -dynamiclib -o $BUILD_DIR/libtestsymbol1.dylib      -install_name $RUN_DIR/libtestsymbol1.dylib $BUILD_DIR/libgood.dylib -framework Foundation $BUILD_DIR/hide/libtestsymbol2extra.dylib
// BUILD:  $CC a.c -dynamiclib -o $BUILD_DIR/libtestsymbol.dylib       -install_name $RUN_DIR/libtestsymbol.dylib $BUILD_DIR/libtestsymbol1.dylib
// BUILD:  $CC c.c -dynamiclib -o $BUILD_DIR/hide/libtestlib2.dylib    -install_name $RUN_DIR/libtestlib2.dylib -DEXTRA_SYMBOL=1
// BUILD:  $CC b.m -dynamiclib -o $BUILD_DIR/libtestlib1.dylib         -install_name $RUN_DIR/libtestlib1.dylib $BUILD_DIR/libgood.dylib -framework Foundation $BUILD_DIR/hide/libtestlib2.dylib
// BUILD:  $CC a.c -dynamiclib -o $BUILD_DIR/libtestlib.dylib          -install_name $RUN_DIR/libtestlib.dylib $BUILD_DIR/libtestlib1.dylib
// BUILD:  $CC main.c -DRUN_DIR="$RUN_DIR" -o $BUILD_DIR/dlopen-fail-cleanly.exe

// BUILD: $SKIP_INSTALL $BUILD_DIR/hide/libtestsymbol2extra.dylib
// BUILD: $SKIP_INSTALL $BUILD_DIR/hide/libtestlib2.dylib

// test that dlopen can back out of a dlopen() of a tree of dylibs where a deep dylib fails to load.
//   libtestsymbol.dylib fails because of a missing symbol
//   libtestlib.dylib fails because of a missing dylib


// RUN:  ./dlopen-fail-cleanly.exe

#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>

#include "test_support.h"

int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    // dlopen dylib chain that should fail
    void* handle = dlopen(RUN_DIR "/libtestsymbol.dylib", RTLD_NOW); // use RTLD_NOW to force missing symbol
    if ( handle != NULL ) {
        FAIL("dlopen(libtestsymbol.dylib) expected to fail but did not");
    }

    // dlopen dylib chain that should fail
    handle = dlopen(RUN_DIR "/libtestlib.dylib", RTLD_LAZY);
    if ( handle != NULL ) {
        FAIL("dlopen(libtestlib.dylib) expected to fail but did not");
    }

    // iterate loaded images and make sure no residue from failed dlopen
    const char* foundPath = NULL;
    int count = _dyld_image_count();
    for (int i=0; i < count; ++i) {
        const char* path = _dyld_get_image_name(i);
        //LOG("path[%2d]=%s", i, path);
        if ( strstr(path, RUN_DIR "/lib") != NULL ) {
            FAIL("Found unexpected loaded image: %s", path);
        }
    }

    // try again to make sure it still fails
    handle = dlopen(RUN_DIR "/libtestsymbol.dylib", RTLD_NOW); // use RTLD_NOW to force missing symbol
    if ( handle != NULL ) {
        FAIL("dlopen(libtestsymbol.dylib) expected to fail but did not");
    }

    // try again to make sure it still fails
    handle = dlopen(RUN_DIR "/libtestlib.dylib", RTLD_LAZY);
    if ( handle != NULL ) {
        FAIL("dlopen(libtestlib.dylib) expected to fail but did not");
    }

    PASS("Success");
}

