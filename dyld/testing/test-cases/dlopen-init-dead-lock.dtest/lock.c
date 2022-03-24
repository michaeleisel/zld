#include <pthread.h>

pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;

void withLock(void (^work)())
{
    pthread_mutex_lock(&sLock);
        work();
    pthread_mutex_unlock(&sLock);
}

