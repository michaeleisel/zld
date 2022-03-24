
#include <stdio.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>

#include "test_support.h"

void jna() {
    // jna should see libFoundation.dylib as it's name is correct
    void* handle = dlopen("libFoundation.dylib", RTLD_LAZY);
    if ( handle == NULL ) {
#if __x86_64__
        FAIL("dlopen-jna, libjna not be able to dlopen(): %s", dlerror());
#endif
    } else {
#if !__x86_64__
        FAIL("dlopen-jna, libjna should not be able to dlopen(): %s", dlerror());
#endif
    }

    // jna should see libSecurity.dylib as it's name is correct
    void* handle2 = dlopen("libSecurity.dylib", RTLD_LAZY);
    if ( handle2 == NULL ) {
#if __x86_64__
        FAIL("dlopen-jna, libjna not be able to dlopen(): %s", dlerror());
#endif
    } else {
#if !__x86_64__
        FAIL("dlopen-jna, libjna should not be able to dlopen(): %s", dlerror());
#endif
    }

    // jna should see libCarbon.dylib as it's name is correct
    void* handle3 = dlopen("libCarbon.dylib", RTLD_LAZY);
    if ( handle3 == NULL ) {
#if __x86_64__
        FAIL("dlopen-jna, libjna not be able to dlopen(): %s", dlerror());
#endif
    } else {
#if !__x86_64__
        FAIL("dlopen-jna, libjna should not be able to dlopen(): %s", dlerror());
#endif
    }
}
