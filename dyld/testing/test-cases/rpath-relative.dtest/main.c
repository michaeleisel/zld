
// BUILD:  $CC bar.c -dynamiclib -o $BUILD_DIR/librel.dylib -install_name @rpath/librel.dylib
// BUILD:  $CC foo.c -dynamiclib -o $BUILD_DIR/other/libfoo.dylib -install_name $RUN_DIR/other/libfoo.dylib $BUILD_DIR/librel.dylib -rpath /junk/
// BUILD:  $CC main.c $BUILD_DIR/other/libfoo.dylib  -o $BUILD_DIR/rpath-executable.exe       -rpath @executable_path
// BUILD:  $CC main.c $BUILD_DIR/other/libfoo.dylib  -o $BUILD_DIR/rpath-executable-slash.exe -rpath @executable_path/
// BUILD:  $CC main.c $BUILD_DIR/other/libfoo.dylib  -o $BUILD_DIR/rpath-loader.exe           -rpath @loader_path
// BUILD:  $CC main.c $BUILD_DIR/other/libfoo.dylib  -o $BUILD_DIR/rpath-loader-slash.exe     -rpath @loader_path/

// RUN: ./rpath-executable.exe
// RUN: ./rpath-executable-slash.exe
// RUN: ./rpath-loader.exe
// RUN: ./rpath-loader-slash.exe

// Main prog links with other/libfoo.dylib which links with @rpath/ibrel.dylib.
// Main prog has LC_RPATH of main executable dir (in four variants)
// librel.dyib has an LC_RPATH of /junk to make sure @loader_path is expanded properly

#include <stdio.h>

#include "test_support.h"

extern char* __progname;


int main(int argc, const char* argv[])
{
    PASS("%s", __progname);
}


