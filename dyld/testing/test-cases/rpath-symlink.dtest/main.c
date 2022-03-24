
// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/hide/lib/libfoo.dylib -install_name @rpath/libfoo.dylib
// BUILD:  $CC main.c $BUILD_DIR/hide/lib/libfoo.dylib  -o $BUILD_DIR/hide/bin/rpath.exe -rpath @executable_path/../lib/
// BUILD:  $SYMLINK ./hide/bin/rpath.exe $BUILD_DIR/rpath-symlink.exe  $DEPENDS_ON_ARG $BUILD_DIR/hide/bin/rpath.exe

// RUN: ./rpath-symlink.exe
// RUN: ./hide/bin/rpath.exe

// Main prog is executed via a symlink path, but @rpath depends on path being real


#include <stdio.h>

#include "test_support.h"

extern char* __progname;

int main(int argc, const char* argv[])
{
    PASS("%s", __progname);
}


