
// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/libfoo.dylib
// BUILD:  $CC main.c -o $BUILD_DIR/dlopen-in-inserted-dylib.exe

// RUN:  DYLD_INSERT_LIBRARIES=$RUN_DIR/libfoo.dylib ./dlopen-in-inserted-dylib.exe

#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>

#include "test_support.h"


int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
	// If we get this far, we didn't crash when the inserted dylib dlopen()ed and caused the state.loaded to grow
    PASS("Success");
}

