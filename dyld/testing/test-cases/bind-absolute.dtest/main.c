
// BUILD:  $CC foo.c -dynamiclib  -install_name $RUN_DIR/libfoo.dylib -o $BUILD_DIR/libfoo.dylib
// BUILD:  $CC main.c $BUILD_DIR/libfoo.dylib -o $BUILD_DIR/bind-absolute.exe

// RUN:  ./bind-absolute.exe

// Verify that large absolute values are encoded correctly

#include <stdio.h>

#include "test_support.h"

extern const struct { char c; } abs_value;

// Choose a large enough negative offset to be before the shared cache or the image
void* bind = &abs_value;

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
    if ( (uintptr_t)bind != (uintptr_t)0xF000000000000000ULL ) {
        FAIL("bind-absolute: %p != %p", bind, (void*)0xF000000000000000ULL);
    }

    PASS("bind-absolute");
}

