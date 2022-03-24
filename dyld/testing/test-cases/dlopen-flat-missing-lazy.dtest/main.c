
// BUILD(macos,ios,tvos,bridgeos|x86_64,arm64):  $CC foo.c -dynamiclib           -Wl,-undefined,dynamic_lookup -install_name $RUN_DIR/libfoo.dylib -Wl,-no_fixup_chains -o $BUILD_DIR/libfoo.dylib
// BUILD(macos,ios,tvos,bridgeos|x86_64,arm64):  $CC bar.c -dynamiclib           -Wl,-undefined,dynamic_lookup -install_name $RUN_DIR/libbar.dylib -Wl,-no_fixup_chains -o $BUILD_DIR/libbar.dylib
// BUILD(macos,ios,tvos,bridgeos|x86_64,arm64):  $CC baz.c -dynamiclib           -Wl,-undefined,dynamic_lookup -install_name $RUN_DIR/libbaz.dylib -Wl,-no_fixup_chains -o $BUILD_DIR/libbaz.dylib
// BUILD(macos,ios,tvos,bridgeos|x86_64,arm64):  $CC main.c -DRUN_DIR="$RUN_DIR" -Wl,-undefined,dynamic_lookup                                     -Wl,-no_fixup_chains -o $BUILD_DIR/dlopen-flat-missing-lazy.exe

// BUILD(watchos):

// RUN(macos,ios,tvos,bridgeos|x86_64,arm64):  ./dlopen-flat-missing-lazy.exe

// At launch, any missing flat, lazy symbols in the main executable and dylibs will be bound to the abort handler
// After dlopen, we try bind again, just in case a defintion exists

#include <stdio.h>
#include <dlfcn.h>

#include "test_support.h"

extern int foo();
extern int bar();

typedef __typeof(&foo) fooPtr;

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {

    // Try dlopen libbaz.dylib.  We expect it to fail due to missing symbols
    // When it fails, dyld should clean up the missing symbol entry for libbaz.dylib -> foo()
    void* bazHandle = dlopen(RUN_DIR "/libbaz.dylib", RTLD_LAZY);
    if ( bazHandle != NULL )
        FAIL("dlopen(libbaz.dylib) should not have succeeded");

    // Foo exports foo()
    void* fooHandle = 0;
    {
        fooHandle = dlopen(RUN_DIR "/libfoo.dylib", RTLD_LAZY);
        if (!fooHandle) {
            FAIL("dlopen(\"" RUN_DIR "/libfoo.dylib\") failed with error: %s", dlerror());
        }
    }

    // Calling foo() should now work, if it has been bound after the dlopen
    int fooResult = foo();
    if ( fooResult != 42 )
        FAIL("foo() should have returned 42.  Returned %d instead", fooResult);

    // dlclose libfoo. This should remove the fooCallsBar missing symbol entry
    int result = dlclose(fooHandle);
    if ( result != 0 )
        FAIL("Expected dlclose(libfoo.dylib) to succeed");

    // dlopen libbar which has the bar() symbol
    void* barHandle = 0;
    {
        barHandle = dlopen(RUN_DIR "/libbar.dylib", RTLD_LAZY);
        if (!barHandle) {
            FAIL("dlopen(\"" RUN_DIR "/libbar.dylib\") failed with error: %s", dlerror());
        }
    }

    // Calling bar() should now work, if it has been bound after the dlopen
    int barResult = bar();
    if ( barResult != 43 )
        FAIL("bar() should have returned 43.  Returned %d instead", barResult);

    PASS("Success");
}

