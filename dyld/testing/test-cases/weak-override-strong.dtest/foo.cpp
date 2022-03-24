
#include <stdexcept>
#include <stdio.h>
#include <string>

#include "test_support.h"

bool gUsedFooNew = false;

// get a strong definition of this C++ symbol
__attribute__((used))
void* operator new(size_t size)
{
	gUsedFooNew = true;
	return malloc(size);
}