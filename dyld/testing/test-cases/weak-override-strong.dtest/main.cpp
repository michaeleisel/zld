
// BUILD:  $CC foo.cpp -Wno-missing-exception-spec -lc++ -dynamiclib -o $BUILD_DIR/libfoo.dylib -install_name $RUN_DIR/libfoo.dylib -Wl,-no_fixup_chains
// BUILD:  $CC main.cpp -Wno-missing-exception-spec -o $BUILD_DIR/weak-override-strong.exe -Wl,-no_fixup_chains -DRUN_DIR="$RUN_DIR"  -lc++ -L$BUILD_DIR -lfoo

// RUN:  ./weak-override-strong.exe

// Find a strong definition of a symbol after libc++.  We patch libc++ late enough to use the strong definition

#include <dlfcn.h>
#include <stdexcept>
#include <stdio.h>
#include <string>

#include "test_support.h"

bool gUsedMainNew = false;
extern bool gUsedFooNew;

__attribute__((weak))
void* operator new(size_t size)
{
	gUsedMainNew = true;
	return malloc(size);
}


int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
	// We shouldn't be using new from the main executable
	int* newInt = new int(1);
	delete newInt;

	if ( gUsedMainNew )
		FAIL("Excected new int(1) not to use the main.exe new()");

	if ( !gUsedFooNew )
		FAIL("Excected new int(1) to use the libfoo.dylib new()");

	// std::string operations like append are implemented in libc++, so we can use them
	// to get a use of libc++.
	gUsedMainNew = false;
	gUsedFooNew = false;
	std::string str;
	str.resize(10000);

	if ( gUsedMainNew )
		FAIL("Excected std::string append not to use the main.exe new()");

	if ( !gUsedFooNew )
		FAIL("Excected std::string append to call new()");

    PASS("Success");
}

