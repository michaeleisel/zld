

#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <dispatch/dispatch.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>

extern struct mach_header __dso_handle;

extern int __cxa_atexit(void (*func)(void*), void* arg, void* dso);


bool  gRanInit = false;
bool* gRanTerm = NULL;

#define SUPPORT_CUSTOM_SEGMENTS !(__arm64e__ || (__arm64__ && __ARM64_ARCH_8_32__))


#if SUPPORT_CUSTOM_SEGMENTS
__attribute__((section(("__MORETEXT,__text"))))
#endif
void myterm()
{
	if ( gRanTerm != NULL )
		*gRanTerm = true;
}


#if SUPPORT_CUSTOM_SEGMENTS
__attribute__((section(("__SOMETEXT,__text"))))
#endif
__attribute__((constructor))
void myinit()
{
	gRanInit = true;
    // register terminator to run when this dylib is unloaded
    __cxa_atexit(&myterm, NULL, &__dso_handle);
}

bool foo(bool* ptr)
{
	if (!gRanInit)
		return false;
	gRanTerm = ptr;
	return true;
}

