
#include <mach-o/dyld-interposing.h>

extern int foo2();
extern int foo4() __attribute__((weak_import));
extern int foo6();



int myfoo2() { return 12; }
int myfoo4() { return 14; }
int myfoo6() { return 16; }



DYLD_INTERPOSE(myfoo2, foo2)
DYLD_INTERPOSE(myfoo4, foo4)
DYLD_INTERPOSE(myfoo6, foo6)
