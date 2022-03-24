
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>

#include "test_support.h"

// dlopen(RTLD_NOLOAD) a bunch of paths on a thread
static void* work(void* mh)
{
    // test an already loaded image
    void* h = dlopen("/usr/lib/libSystem.dylib", RTLD_NOLOAD);
    if ( h == NULL )
        FAIL("dlopen(\"/usr/lib/libSystem.dylib\", RTLD_NOLOAD) returned NULL");

    // test a symlink to an already loaded image
    h = dlopen("/usr/lib/libc.dylib", RTLD_NOLOAD);
    if ( h == NULL )
        FAIL("dlopen(\"/usr/lib/libc.dylib\", RTLD_NOLOAD) returned NULL");

    // test something not loaded
    h = dlopen("/foo/bad/path/junk.dylib", RTLD_NOLOAD);
    if ( h != NULL )
        FAIL("dlopen(\"/foo/bad/path/junk.dylib\", RTLD_NOLOAD) returned non-NULL");
}


void __attribute__((constructor))
myInit()
{
    pthread_t workerThread;

    // call dlopen(RTLD_NOLOAD) on another thread in this initializer
    // this will hang unless dyld does not use API lock for RTLD_NOLOAD
    if ( pthread_create(&workerThread, NULL, &work, NULL) != 0 ) {
        FAIL("pthread_create");
    }

    void* dummy;
    pthread_join(workerThread, &dummy);
}


