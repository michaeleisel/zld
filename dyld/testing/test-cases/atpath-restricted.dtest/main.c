
// BUILD(macos):  $CC foo.c -dynamiclib -o $BUILD_DIR/test/libfoo.dylib -install_name @rpath/libfoo.dylib
// BUILD(macos):  $CC main.c -o $BUILD_DIR/atpath-restricted.exe -Wl,-rpath,./test/ $BUILD_DIR/test/libfoo.dylib -DRESTRICTED=1 -sectcreate __RESTRICT __restrict /dev/null
// BUILD(macos):  $CC main.c -o $BUILD_DIR/atpath-unrestricted.exe  -Wl,-rpath,./test/ $BUILD_DIR/test/libfoo.dylib -DRESTRICTED=0

// BUILD(ios,tvos,watchos,bridgeos):

// RUN:  ./atpath-restricted.exe
// RUN:  ./atpath-unrestricted.exe

#include <stdio.h>
#include <dlfcn.h>

#include "test_support.h"

__attribute__((weak_import))
extern void foo();

int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
#if __x86_64__ && !RESTRICTED
    // Unrestricted x86_64 processes should be able to find foo via a relative rpath
    if ( &foo == NULL )
        FAIL("Expected &foo to be non-null");
#else
  // The @rpath link to foo should fail when we are restricted (or not x86_64), so we expect it to be null
  if ( &foo != NULL )
      FAIL("Expected &foo to be null");
#endif
    PASS("Success");
}

