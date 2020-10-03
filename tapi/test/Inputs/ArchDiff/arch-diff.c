#include "arch-diff.h"

#if __x86_64h__
void x86_64h_func(void) {}
#else
void general_func(void) {}
#endif
