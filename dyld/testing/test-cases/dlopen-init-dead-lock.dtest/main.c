
// BUILD:  $CC lock.c -dynamiclib -o $BUILD_DIR/liblock.dylib -install_name $RUN_DIR/liblock.dylib
// BUILD:  $CC foo.c -bundle -o $BUILD_DIR/foo.bundle $BUILD_DIR/liblock.dylib
// BUILD:  $CC main.c -o $BUILD_DIR/dlopen-dead-lock.exe $BUILD_DIR/liblock.dylib  -DRUN_DIR="$RUN_DIR"

// RUN:  ./dlopen-dead-lock.exe

// We are testing that dlopen() releases the loaders-lock when running initializers.
// Otherwise the loaders-lock may dead lock with other locks in other code.



#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <mach-o/dyld_priv.h>

#include "test_support.h"

extern void withLock(void (^work)());

static void* work1(void* mh)
{
    for (int i=0; i < 100; ++i) {
        void* h = dlopen(RUN_DIR "/foo.bundle", 0);
        usleep(20);
        dlclose(h);
    }
    return NULL;
}


static void notify(const struct mach_header* mh, intptr_t vmaddr_slide)
{
    // skip images in shared cache
    if ( mh->flags & 0x80000000 )
        return;

    usleep(50);
}

static void* work2(void* mh)
{
    // have main thread repeatedly call _dyld_register_func_for_add_image()
    for (int i=0; i < 100; ++i) {
        withLock(^{
            _dyld_register_func_for_add_image(&notify);
        });
        usleep(20);
   }
    return NULL;
}


int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    pthread_t workerThread1;
    pthread_t workerThread2;

    // Make a thread for dlopen() and one for _dyld_register_func_for_add_image()
    if ( pthread_create(&workerThread1, NULL, &work1, NULL) != 0 ) {
        FAIL("pthread_create");
    }
    if ( pthread_create(&workerThread2, NULL, &work2, NULL) != 0 ) {
        FAIL("pthread_create");
    }

    void* dummy;
    pthread_join(workerThread1, &dummy);
    pthread_join(workerThread2, &dummy);

    PASS("Success");
}

