
// BUILD:  $CC bar.c -dynamiclib           -install_name $RUN_DIR/libbar.dylib -o $BUILD_DIR/libbar.dylib
// BUILD:  $CC foo.m -dynamiclib           -install_name $RUN_DIR/libfoo.dylib -o $BUILD_DIR/libfoo.dylib $BUILD_DIR/libbar.dylib
// BUILD:  $CC baz.c -dynamiclib           -install_name $RUN_DIR/libbaz.dylib -o $BUILD_DIR/libbaz.dylib $BUILD_DIR/libbar.dylib $BUILD_DIR/libfoo.dylib
// BUILD:  $CC main.c -DRUN_DIR="$RUN_DIR"                                     -o $BUILD_DIR/dlclose-never-unload-deps2.exe

// RUN:  ./dlclose-never-unload-deps2.exe

// Make sure that dependents of never unload binaries are also never unloaded
// In this case we have:
// libbaz.dylib -----------------------------> libbar.dylib
//              \------- libfoo.dylib -------/
//
// We dlopen libbaz.dylib, then dlclose it.
// libfoo.dylib contains objc so is set to neverUnload.  As libbar.dylib is
// a dependency of libfoo.dylib, it should also stay loaded

#include <stdio.h>
#include <dlfcn.h>
#include <mach-o/dyld_priv.h>

#include "test_support.h"

static bool imageIsLoaded(const char* pathToFind)
{
    int count = _dyld_image_count();
    bool foundPath = false;
    for (int i=0; i < count; ++i) {
        const char* path = _dyld_get_image_name(i);
        LOG("path[%2d]=%s", i, path);
        if ( strcmp(path, pathToFind) == 0 ) {
            foundPath = true;
        }
    }
    return foundPath;
}

static void assertImageIsLoaded(const char* pathToFind)
{
    if ( !imageIsLoaded(pathToFind) )
        FAIL("Didn't find %s", pathToFind);
}

static void assertImageIsNotLoaded(const char* pathToFind)
{
    if ( imageIsLoaded(pathToFind) )
        FAIL("Didn't expect to find %s", pathToFind);
}

extern int foo();
typedef __typeof(&foo) fooPtr;
extern fooPtr baz();
typedef __typeof(&baz) bazPtr;

int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    void* handle = dlopen(RUN_DIR "/libbaz.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlopen(\"libbaz.dylib\"), dlerror()=%s", dlerror());
    }

    assertImageIsLoaded(RUN_DIR "/libbaz.dylib");
    assertImageIsLoaded(RUN_DIR "/libfoo.dylib");
    assertImageIsLoaded(RUN_DIR "/libbar.dylib");

    bazPtr bazSym = (bazPtr)dlsym(handle, "baz");
    if ( bazSym == NULL ) {
        FAIL("dlsym(\"baz\") returned NULL, dlerror()=%s", dlerror());
    }
    fooPtr fooSym = bazSym();

    if ( fooSym() != 42 ) {
        FAIL("Expected 42 on the first call to foo()");
    }

    int result = dlclose(handle);
    if ( result != 0 ) {
        FAIL("dlclose(handle) returned %d, dlerror()=%s", result, dlerror());
    }

    // libbaz.dylib is unloadable, so it should have been removed by the dlclose()
    assertImageIsNotLoaded(RUN_DIR "/libbaz.dylib");
    assertImageIsLoaded(RUN_DIR "/libfoo.dylib");
    assertImageIsLoaded(RUN_DIR "/libbar.dylib");

    // Call foo()->bar() again.  It should not fail.
    if ( fooSym() != 42 ) {
        FAIL("Expected 42 on the second call to foo()");
    }

    PASS("Success");
}

