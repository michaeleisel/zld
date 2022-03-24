// BUILD(macos):  $CC main.c -o $BUILD_DIR/dlopen-dsc-extractor.exe

// BUILD(ios,tvos,watchos,bridgeos):

// RUN:  ./dlopen-dsc-extractor.exe

#include "test_support.h"
#include <dlfcn.h>

int main(int arg, const char* argv[])
{
    void* handle = dlopen("/usr/lib/dsc_extractor.bundle", RTLD_LAZY);
    if ( handle == NULL )
        FAIL("dlopen-dsc-extractor: dlopen() should have succeeded");
    else
    	PASS("Success");
	return 0;
}

