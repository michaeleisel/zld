// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/libfoo.dylib -install_name $RUN_DIR/libfoo.dylib
// BUILD:  $CC main.c $BUILD_DIR/libfoo.dylib -o $BUILD_DIR/interpose-chained.exe
// BUILD:  $DYLD_ENV_VARS_ENABLE $BUILD_DIR/interpose-chained.exe
// BUILD:  $CC foo1.c -dynamiclib -o $BUILD_DIR/libfoo1.dylib -install_name libfoo1.dylib $BUILD_DIR/libfoo.dylib
// BUILD:  $CC foo2.c -dynamiclib -o $BUILD_DIR/libfoo2.dylib -install_name libfoo2.dylib $BUILD_DIR/libfoo.dylib
// BUILD:  $CC foo3.c -dynamiclib -o $BUILD_DIR/libfoo3.dylib -install_name libfoo3.dylib $BUILD_DIR/libfoo.dylib

// RUN:    DYLD_INSERT_LIBRARIES=libfoo1.dylib:libfoo2.dylib:libfoo3.dylib    ./interpose-chained.exe

//
// This unit test verifies that multiple interposing libraries can all interpose the same function
// and the result is that they chain together. That is, each one calls through to the next.
//
// The function foo() does string appends.  This allows us to check:
// 1) every interposer was called, and 2) they were called in the correct order.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"
#include "foo.h"

int main()
{
	const char* x = foo("seed");
  
	if ( strcmp(x, "foo3(foo2(foo1(foo(seed))))") == 0 )
		PASS("interpose-chained");
	else 
		FAIL("interpose-chained %s", x);
	return EXIT_SUCCESS;
}
