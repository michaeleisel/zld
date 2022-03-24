
// BUILD:  $CC bar.cpp -Wno-missing-exception-spec -lc++ -dynamiclib -o $BUILD_DIR/libbar.dylib -install_name $RUN_DIR/libbar.dylib -Wl,-no_fixup_chains
// BUILD:  $CC foo.cpp -Wno-missing-exception-spec -lc++ -dynamiclib -o $BUILD_DIR/libfoo.dylib -install_name $RUN_DIR/libfoo.dylib -Wl,-no_fixup_chains $BUILD_DIR/libbar.dylib
// BUILD:  $CC main.cpp -Wno-missing-exception-spec -lc++ -o $BUILD_DIR/weak-override-shared-cache-dlopen.exe -Wl,-no_fixup_chains -DRUN_DIR="$RUN_DIR"

// RUN:  ./weak-override-shared-cache-dlopen.exe

// dlopen a strong definition of a symbol.  We shouldn't patch the shared cache if its already been patched
// Note this doesn't fix the problem that the strong definition is different from the other binaries

#include <dlfcn.h>
#include <stdexcept>
#include <stdio.h>
#include <string>

#include "test_support.h"

bool gUsedNew = false;

__attribute__((weak))
void* operator new(size_t size)
{
	gUsedNew = true;
	return malloc(size);
}


int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
	// Check if we are using our new.  If we aren't then the rest of the test doesn't really work
	int* newInt = new int(1);
	delete newInt;

	if ( !gUsedNew )
		PASS("Success");

	// std::string operations like append are implemented in libc++, so we can use them
	// to get a use of libc++.
	gUsedNew = false;
	std::string str;
	str.resize(10000);

	if ( !gUsedNew )
		FAIL("Excected std::string append (1) to call new()");

    // dlopen foo.  We shouldn't patch the cache to point to the new() in libfoo
    void* handle = dlopen(RUN_DIR "/libfoo.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlopen(\"libfoo.dylib\", RTLD_LAZY) failed but it should have worked: %s", dlerror());
    }

    // Check the string again.  If shouldn't call libfoo's new
    gUsedNew = false;
	std::string str2;
	str2.resize(10000);

	if ( !gUsedNew )
		FAIL("Excected std::string append (2) to call new()");

    PASS("Success");
}

