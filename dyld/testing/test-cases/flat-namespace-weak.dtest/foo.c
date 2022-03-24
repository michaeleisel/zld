
#include <stdlib.h>
#include <string.h>

extern void bar() __attribute__((weak_import));

void* pbar = &bar;


