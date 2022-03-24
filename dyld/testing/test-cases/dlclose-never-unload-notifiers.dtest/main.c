
// BUILD:  $CC add.c -dynamiclib  -install_name $RUN_DIR/libfoo.dylib -o $BUILD_DIR/libadd.dylib
// BUILD:  $CC remove.c -dynamiclib  -install_name $RUN_DIR/libfoo.dylib -o $BUILD_DIR/libremove.dylib
// BUILD:  $CC load.c -dynamiclib  -install_name $RUN_DIR/libfoo.dylib -o $BUILD_DIR/libload.dylib
// BUILD:  $CC bulkload.c -dynamiclib  -install_name $RUN_DIR/libfoo.dylib -o $BUILD_DIR/libbulkload.dylib
// BUILD:  $CC main.c -o $BUILD_DIR/dlclose-never-unload-notifiers.exe -DRUN_DIR="$RUN_DIR"

// RUN:  ./dlclose-never-unload-notifiers.exe

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_priv.h>

#include "test_support.h"

extern void registerNotifier();

bool unloadedImage = false;

static void notifyUnload(const struct mach_header* mh, intptr_t vmaddr_slide)
{
    unloadedImage = true;
}

void doImage(const char* path)
{
    void* handle = dlopen(path, RTLD_FIRST);
    if ( handle == NULL ) {
        FAIL("dlopen(\"%s\") failed with: %s", path, dlerror());
    }
    __typeof(&registerNotifier) func = (__typeof(&registerNotifier))dlsym(handle, "registerNotifier");

    func();

    // dlclose here should be a nop
    dlclose(handle);

    if ( unloadedImage )
        FAIL("Didn't expect to unload an image when we closed %s\n", path);
}


int main(int argc, const char* argv[], const char* envp[], const char* apple[])
{
    _dyld_register_func_for_remove_image(&notifyUnload);

    doImage(RUN_DIR "/libadd.dylib");
    doImage(RUN_DIR "/libremove.dylib");
    doImage(RUN_DIR "/libload.dylib");
    doImage(RUN_DIR "/libbulkload.dylib");

    PASS("Success");
}

