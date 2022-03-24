
// BUILD:  $CC foo.cpp -Wno-missing-exception-spec -lc++ -dynamiclib -o $BUILD_DIR/libfoo.dylib -install_name $RUN_DIR/libfoo.dylib -Wl,-no_fixup_chains
// BUILD:  $CC bar.cpp -Wno-missing-exception-spec -dynamiclib -o $BUILD_DIR/libbar.dylib -install_name $RUN_DIR/libbar.dylib -Wl,-no_fixup_chains -lc++ -L$BUILD_DIR -lfoo
// BUILD:  $CC main.cpp -Wno-missing-exception-spec -o $BUILD_DIR/weak-override-strong2.exe -DRUN_DIR="$RUN_DIR"  -lc++ -L$BUILD_DIR -lbar -Wl,-no_fixup_chains -fno-stack-protector -fno-stack-check

// RUN:  ./weak-override-strong2.exe

// The __strong weak-bind opcodes in libfoo.dylib should override libc++.dylib in the shared cache.

#include <stdio.h>

#include "test_support.h"

extern bool bar();


int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
	bool usedFooNew = bar();

	// Only macOS checks dylibs for strong overrides of weak symbols
#if TARGET_OS_OSX
	if ( !usedFooNew ) {
		FAIL("Excected std::string append to call libfoo.dylib's new()");
	}
#else
	if ( usedFooNew ) {
		FAIL("Excected std::string append to not call libfoo.dylib's new()");
	}
#endif

    PASS("Success");
}

