
#include <unistd.h>
#include <pthread.h>

extern void withLock(void (^work)());

void __attribute__((constructor))
myInit()
{
    withLock(^ {
        usleep(1000);
    });
}

void foo()
{
}

