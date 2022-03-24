
#include <stdio.h>
#include <mach-o/dyld-interposing.h>

#include "foo.h"

const char* foo3(const char* str)
{
	char* result;
	asprintf(&result, "foo3(%s)", foo(str));
	return result;
}

DYLD_INTERPOSE(foo3, foo)
