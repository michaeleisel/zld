
#include <stdio.h>
#include <mach-o/dyld-interposing.h>

#include "foo.h"

const char* foo1(const char* str)
{
	char* result;
	asprintf(&result, "foo1(%s)", foo(str));
	return result;
}

DYLD_INTERPOSE(foo1, foo)
