#include <stdbool.h>

#include "test_support.h"

static bool initRun = false;

__attribute__((constructor))
void myinit()
{
    if ( initRun )
        FAIL("initializer run twice");

    initRun = true;
}

