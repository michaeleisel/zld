
// BUILD:  $CC foo.c -bundle -o $BUILD_DIR/foo.bundle
// BUILD:  $CC bar.c -bundle -o $BUILD_DIR/bar.bundle
// BUILD:  $CC main.c -o $BUILD_DIR/dyld_fork_test2.exe -DRUN_DIR="$RUN_DIR"

// RUN:  ./dyld_fork_test2.exe 

#include <stdio.h>
#include <dlfcn.h>
#include <mach-o/dyld_priv.h>
#include <pthread.h>

#include "test_support.h"

// Check for deadlock between dlopen and atfork_prepare
// dlopen takes locks in the following order:
//   API, loader, notifier
// atfork_prepare (at time of writing) did:
//   loader, notifier, API
// which leads to deadlock as those are not the same order

bool isParent = true;
static void* work1(void* mh)
{
    // fork and exec child
    pid_t sChildPid = fork();
    if ( sChildPid < 0 ) {
        FAIL("Didn't fork");
    }
    if ( sChildPid == 0 ) {
        // child side
        isParent = false;
    }

    return 0;
}

pthread_t workerThread1;

bool runNotifier;
static void notifyThread0(const struct mach_header* mh, intptr_t vmaddr_slide)
{
    if (!runNotifier)
        return;

    runNotifier = false;

    // We are in a notifier in a dlopen, so the API and notifier locks are held.

    // Spawn a thread to do a fork, which will take the available locks, ie, maybe the loader lock
    workerThread1;
    if ( pthread_create(&workerThread1, NULL, &work1, NULL) != 0 ) {
        FAIL("pthread_create");
    }

    // Wait for a short time, to make sure the fork took any available locks
    sleep(1);

    // Do another dlopen, which will deadlock as the fork thread has some locks
    void* h = dlopen(RUN_DIR "/bar.bundle", 0);
    if ( h == NULL )
        FAIL("Failed to dlopen bar.bundle because: %s\n", dlerror());
}

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
    runNotifier = false;
    _dyld_register_func_for_add_image(&notifyThread0);
    
    // Do a dlopen to trigger the notifier again
    runNotifier = true;
    void* h = dlopen(RUN_DIR "/foo.bundle", 0);
    if ( h == NULL )
        FAIL("Failed to dlopen foo.bundle because: %s\n", dlerror());

    void* dummy;
    pthread_join(workerThread1, &dummy);

    if (isParent) {
        // dlopen to make sure we can use the locks after fork()
        void* handle  = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_LAZY);
        if ( handle == NULL )
            FAIL("Could not dlopen CoreFoundation because: %s", dlerror());

        PASS("Success");
    }

    return 0;
}
