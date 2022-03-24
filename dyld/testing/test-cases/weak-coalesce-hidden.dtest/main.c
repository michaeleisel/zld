
// BUILD:  $CC fake_bar.c -dynamiclib -install_name $RUN_DIR/libbar.dylib -o $BUILD_DIR/libfakebar.dylib -Wl,-no_fixup_chains
// BUILD:  $CC foo.c -dynamiclib -install_name $RUN_DIR/libfoo.dylib -o $BUILD_DIR/libfoo.dylib -Wl,-no_fixup_chains $BUILD_DIR/libfakebar.dylib
// BUILD:  $CC bar.c -dynamiclib -install_name $RUN_DIR/libbar.dylib -o $BUILD_DIR/libbar.dylib -Wl,-no_fixup_chains
// BUILD:  $CC main.c -o $BUILD_DIR/weak-coalesce-hidden.exe -Wl,-no_fixup_chains -DRUN_DIR="$RUN_DIR"

// RUN:  ./weak-coalesce-hidden.exe

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include "test_support.h"

extern int foo();

extern void* getNullable();

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {

	// dlopen libbar with LOCAL.  That hides its symbols from weak def coalescing
	void* handle = dlopen(RUN_DIR "/libbar.dylib", RTLD_LOCAL);
    if ( handle == NULL ) {
        FAIL("dlopen(\"libbar.dylib\", RTLD_LOCAL) failed but it should have worked: %s", dlerror());
    }

	// Then dlopen libfoo.  It won't be able to find bar() as a weak def, but the regular
	// bind should work when using opcode baseed fixups
    void* handle2 = dlopen(RUN_DIR "/libfoo.dylib", RTLD_GLOBAL);
#if __arm64e__
    // With chained fixups, the dlopen will fail as we can't find bar()
    if ( handle2 != NULL ) {
        FAIL("dlopen(\"libfoo.dylib\", RTLD_GLOBAL) passed but it should have failed on arm64e");
    }
    const char* dlerrorString = dlerror();
    if ( dlerrorString == NULL )
        FAIL("Expected dlerror string");

    if ( strstr(dlerrorString, "weak-def symbol not found") == NULL )
        FAIL("Expected dlerror string to have missing weak def.  Got '%s'", dlerrorString);
#else
    if ( handle2 == NULL ) {
        FAIL("dlopen(\"libfoo.dylib\", RTLD_GLOBAL) failed but it should have worked: %s", dlerror());
    }

    __typeof(&foo) fooSym = (__typeof(&foo))dlsym(handle2, "foo");
    if ( fooSym == 0 ) {
        FAIL("dlsym(foo) failed");
    }

    int result = fooSym();
    if ( result != 42 )
        FAIL("Expected 42.  Got %d instead\n", result);

    __typeof(&getNullable) nullableSym = (__typeof(&getNullable))dlsym(handle2, "getNullable");
    if ( nullableSym == 0 ) {
        FAIL("dlsym(nullable) failed");
    }

    void* result2 = nullableSym();
    if ( result2 != NULL )
        FAIL("Expected NULL.  Got %p instead\n", result2);
#endif // __arm64e__

    PASS("Success");
}

