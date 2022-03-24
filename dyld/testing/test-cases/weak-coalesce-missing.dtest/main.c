
// BUILD(macos|x86_64):  $CC main.c -o $BUILD_DIR/weak-coalesce-missing.exe -Wl,-no_fixup_chains

// BUILD(macos|x86_64):  $STRIP $BUILD_DIR/weak-coalesce-missing.exe -R $SRC_DIR/symbols-to-strip.txt

// Only macOS strip removes exports from the trie, so we can only run this test on macOS
// BUILD(ios,tvos,watchos,bridgeos):

// RUN(macos|x86_64):  ./weak-coalesce-missing.exe


#include <stdio.h>
#include <stdlib.h>

#include "test_support.h"

__attribute__((weak))
int missingSymbol = 42;

int* missingSymbolPtr = &missingSymbol;

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
    // If we get this far, then we didn't abort on launch due to the unexported weak symbol

	// We won't find this symbol anywhere, so it should be 42
    if ( *missingSymbolPtr != 42 )
		FAIL("Expected 42.  Got %d instead\n", *missingSymbolPtr);

    PASS("Success");
}

