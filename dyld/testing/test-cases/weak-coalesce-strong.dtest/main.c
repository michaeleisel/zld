
// BUILD:  $CC foo2.c -Wl,-no_fixup_chains -dynamiclib -install_name $RUN_DIR/libfoo2.dylib 													 -o $BUILD_DIR/libfoo2.dylib
// BUILD:  $CC foo1.c -Wl,-no_fixup_chains -dynamiclib -install_name $RUN_DIR/libfoo1.dylib $BUILD_DIR/libfoo2.dylib 					     -o $BUILD_DIR/libfoo1.dylib
// BUILD:  $CC main.c -Wl,-no_fixup_chains  												   $BUILD_DIR/libfoo1.dylib $BUILD_DIR/libfoo2.dylib -o $BUILD_DIR/weak-coalesce-strong.exe

// RUN:  ./weak-coalesce-strong.exe

// The strong version of coal1 from libfoo1 should be chosen instead of the weak versions in main.exe/libfoo2.dylib


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"

__attribute__((weak))
const char* coal1 = "main";

extern const char* getFoo1Coal1();
extern const char* getFoo2Coal1();

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
    if ( strcmp(coal1, "foo1") != 0 ) {
        FAIL("Expected coal1 from 'foo1', but got '%s' instead\n", coal1);
    }

    // Also check the loaded dylibs to make sure they were coalesced correctly
    if ( strcmp(getFoo1Coal1(), "foo1") != 0 ) {
        FAIL("Expected foo1 coal1 from 'foo1', but got '%s' instead\n", coal1);
    }
    if ( strcmp(getFoo2Coal1(), "foo1") != 0 ) {
        FAIL("Expected foo2 coal1 from 'foo1', but got '%s' instead\n", coal1);
    }

    PASS("Success");
}

