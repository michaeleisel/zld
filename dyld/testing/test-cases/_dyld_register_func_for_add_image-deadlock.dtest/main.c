

// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/libfoo.dylib -install_name $RUN_DIR/libfoo.dylib
// BUILD:  $CC main.c -o $BUILD_DIR/_dyld_register_func_for_add_image-deadlock.exe -DRUN_DIR="$RUN_DIR"

// RUN:  ./_dyld_register_func_for_add_image-deadlock.exe

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <mach-o/dyld.h>

#include "test_support.h"

pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sLock2 = PTHREAD_MUTEX_INITIALIZER;

static void* work1(void* arg)
{
    // Wait for signals, then walk all the mach-o's
    pthread_mutex_lock(&sLock2);
    pthread_mutex_lock(&sLock);
    for (uint32_t i = 0; i != _dyld_image_count(); ++i) {
        // Get each mach-o, which causes dyld to take the read lock
        const struct mach_header* mh = _dyld_get_image_header(i);
    }
    pthread_mutex_unlock(&sLock);
    pthread_mutex_unlock(&sLock2);
    return NULL;
}

bool atLaunch = true;
static void notify(const struct mach_header* mh, intptr_t vmaddr_slide)
{
    if ( atLaunch )
        return;

    // dlopen case.  Signal the worker thread to do something
    pthread_mutex_unlock(&sLock);

    // Now wait on lock2 which tells us the worked thread is done
    pthread_mutex_lock(&sLock2);
}

int main() 
{
    _dyld_register_func_for_add_image(&notify);

    pthread_mutex_lock(&sLock);

    pthread_t workerThread;
    if ( pthread_create(&workerThread, NULL, work1, NULL) != 0 ) {
        FAIL("pthread_create failed\n");
        return 1;
    }

    atLaunch = false;
    void* handle = dlopen(RUN_DIR "/libfoo.dylib", RTLD_FIRST);
    if ( handle == NULL ) {
        FAIL("dlopen(libbar.dylib) failed");
    }

    void* dummy;
    pthread_join(workerThread, &dummy);

    PASS("Success");
}

