
// BUILD(ios,tvos,watchos,bridgeos):  $CC foo.m -dynamiclib -o $BUILD_DIR/libfoo.dylib -lobjc -WL,-install_name,/usr/lib/system/lib/fake_install_name.dylib
// BUILD(ios,tvos,watchos,bridgeos):  $CC main.c -o $BUILD_DIR/dlopen-in-inserted-dylib2.exe

// Don't build test for macOS.  It causes Foundation to crash with unrecognised selectors
// BUILD(macos):

// RUN:  DYLD_INSERT_LIBRARIES=$RUN_DIR/libfoo.dylib ./dlopen-in-inserted-dylib2.exe

#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>

#include "test_support.h"


int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
	// If we get this far, we didn't crash when the inserted dylib dlopen()ed and caused the state.loaded to grow
    PASS("Success");
}

