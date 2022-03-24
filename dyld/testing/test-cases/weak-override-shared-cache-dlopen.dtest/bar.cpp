
#include <stdexcept>
#include <stdio.h>

// We don't use this definition.  We just need it to give resolveSymbol() work to do
__attribute__((weak))
void* operator new(size_t size)
{
	return malloc(size);
}

int* bar()
{
	return new int();
}