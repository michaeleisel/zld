#include <stddef.h>


extern struct mach_header __dso_handle;

extern int __cxa_atexit(void (*func)(void*), void* arg, void* dso);


typedef void (*NotifyProc)(void);

NotifyProc gNotifer = NULL;


static void myTerm()
{
    if ( gNotifer )
        gNotifer();
}


__attribute__((constructor))
void myinit()
{
    // register terminator to run when this dylib is unloaded
    __cxa_atexit(&myTerm, NULL, &__dso_handle);
}

