
#include "test_support.h"

static int state = 0;



__attribute__((constructor))
static void baseInit()
{
    if ( state != 0 )
        FAIL("libbase.dylib initializer not run first");

    state = 1;
}


void fooInitCalled()
{
    if ( state != 1 )
        FAIL("libfoo.dylib initializer not run second");

    state = 2;
}


void mainInitCalled()
{
    if ( state != 2 )
        FAIL("main's initializer not run third");

    state = 3;
}

int getState()
{
    return state;
}
