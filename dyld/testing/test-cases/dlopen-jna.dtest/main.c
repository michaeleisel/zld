// BUILD:  $CC foo.c -dynamiclib -install_name $RUN_DIR/libfoo.dylib  -o $BUILD_DIR/libfoo.dylib
// BUILD:  $CC jna.c -dynamiclib -install_name $RUN_DIR/jna.dylib  -o $BUILD_DIR/jna.dylib
// BUILD:  $CC main.c -o $BUILD_DIR/dlopen-jna.exe $BUILD_DIR/libfoo.dylib $BUILD_DIR/jna.dylib

// RUN:  ./dlopen-jna.exe

#include "test_support.h"

extern void foo();
extern void jna();

int main(int arg, const char* argv[])
{
    foo();
    jna();
    PASS("Success");
	return 0;
}

