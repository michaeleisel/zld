
#include <stdexcept>
#include <stdio.h>
#include <string>

#include "test_support.h"

// get a strong definition of this C++ symbol
__attribute__((used))
void* operator new(size_t size)
{
	FAIL("Shouldn't call new() in libfoo.dylib");
	return malloc(size);
}