
// BUILD:  $CC bar.c -dynamiclib -install_name $RUN_DIR/libbar.dylib -o $BUILD_DIR/libbar.dylib
// BUILD:  $CC foo.c -dynamiclib -install_name $RUN_DIR/libfoo.dylib -o $BUILD_DIR/libfoo.dylib $BUILD_DIR/libbar.dylib
// BUILD:  $CC main.c -o $BUILD_DIR/weakdef-hidden.exe -DRUN_DIR="$RUN_DIR"

// RUN:  ./weakdef-hidden.exe

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include "test_support.h"

extern int an_answer();

int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
	// dlopen libfoo with LOCAL which hides it symbols (but not dylibs below it, including libbar.dylib)
	void* handle = dlopen(RUN_DIR "/libfoo.dylib", RTLD_LOCAL);
    if ( handle == NULL ) {
        FAIL("dlopen(\"libbar.dylib\", RTLD_LOCAL) failed but it should have worked: %s", dlerror());
    }

    // have libfoo call answer()
    __typeof(&an_answer) fooSym = (__typeof(&an_answer))dlsym(handle, "foo_answer");
    if ( fooSym == 0 ) {
        FAIL("dlsym(foo_answer) failed");
    }
    int fooAnswer = fooSym();

    // have libbar call answer()
    __typeof(&an_answer) barSym = (__typeof(&an_answer))dlsym(handle, "bar_answer");
    if ( barSym == 0 ) {
        FAIL("dlsym(bar_answer) failed");
    }
    int barAnswer = barSym();

    // compare answers
    if ( fooAnswer != barAnswer )
        FAIL("foo and bar have different answers: foo_answer() => %d, bar_answer() => %d", fooAnswer, barAnswer);

    PASS("Success");
}

