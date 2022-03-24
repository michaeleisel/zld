
#include <stdio.h>
#include <mach-o/dyld-interposing.h>

#include "foo.h"

const char* foo2(const char* str)
{
	char* result;
	asprintf(&result, "foo2(%s)", foo(str));
	return result;
}

DYLD_INTERPOSE(foo2, foo)
