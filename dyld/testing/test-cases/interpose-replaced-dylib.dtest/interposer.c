#include <stdlib.h>
#include <string.h>
#include <mach-o/dyld-interposing.h>


extern const char* zlibVersion();


const char* myzlibVersion()
{
    return "interposed";
}

DYLD_INTERPOSE(myzlibVersion, zlibVersion)
