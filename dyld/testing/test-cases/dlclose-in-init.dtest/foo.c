#include <dlfcn.h>

#include "test_support.h"

// Calling dlclose() in an initializer shouldn't remove the image, as the ref count should have been
// bumped prior to calling initializers.

__attribute__((constructor))
void myinit()
{
	void* handle = dlopen(RUN_DIR "/libfoo.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("dlopen(libfoo.dylib) expected to pass");
    }

    int result = dlclose(handle);
    if ( result != 0 )
		FAIL("dlclose returned non-zero");
}

