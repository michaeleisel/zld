
#include <stdio.h>

#include "foo.h"

const char* foo(const char* str)
{
	char* result;
	asprintf(&result, "foo(%s)", str);
	return result;
}

