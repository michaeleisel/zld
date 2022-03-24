#include <dlfcn.h>
#include <pthread.h>

#include "test_support.h"

__attribute__((constructor))
void myinit()
{
#if TARGET_OS_OSX
    void* handle = dlopen("/System/Library/Frameworks/AppKit.framework/AppKit", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("AppKit failed to dlopen(): %s", dlerror());
        return;
    }
#else
    void* handle = dlopen("/System/Library/Frameworks/UIKit.framework/UIKit", RTLD_LAZY);
    if ( handle == NULL ) {
        FAIL("UIKit failed to dlopen(): %s", dlerror());
        return;
    }
#endif
}

