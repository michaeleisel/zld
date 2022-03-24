
// BUILD:  $CC main.c            -o $BUILD_DIR/env-DYLD_IMAGE_SUFFIX-asan.exe
// BUILD:  $DYLD_ENV_VARS_ENABLE $BUILD_DIR/env-DYLD_IMAGE_SUFFIX-asan.exe

// RUN:  DYLD_IMAGE_SUFFIX=_asan ./env-DYLD_IMAGE_SUFFIX-asan.exe


#include "test_support.h"

int main(int argc, const char* argv[], const char* envp[], const char* apple[]) {
    PASS("Success");
}

