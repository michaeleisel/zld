
// BOOT_ARGS: amfi=3 cs_enforcement_disable=1

// BUILD(macos,ios,tvos,bridgeos|x86_64,arm64,arm64e):  $CC main.c -o $BUILD_DIR/kernel-fixups.exe -Wl,-static -mkernel -nostdlib -Wl,-add_split_seg_info -Wl,-e,__start -Wl,-pie -Wl,-pagezero_size,0x0 -Wl,-install_name,/usr/lib/swift/split.seg.v2.hack -fno-stack-protector -fno-builtin -ffreestanding -Wl,-segprot,__HIB,rx,rx -Wl,-image_base,0x8000 -Wl,-segaddr,__HIB,0x4000  -fno-ptrauth-function-pointer-type-discrimination -O2
// BUILD(macos,ios,tvos,bridgeos|x86_64,arm64,arm64e):  $APP_CACHE_UTIL -create-kernel-collection $BUILD_DIR/kernel.kc -kernel $BUILD_DIR/kernel-fixups.exe

// BUILD(watchos):


// RUN_STATIC:    $RUN_STATIC ./kernel.kc

#include "../kernel-test-runner.h"
#include "../kernel-fixups.h"
#include "../kernel-classic-relocs.h"

#define printf(...) hostFuncs->printf(__VA_ARGS__)

int x = 1;
int *g = &x;

#if __x86_64__
__attribute__((section(("__HIB, __text"))))
#else
__attribute__((section(("__TEXT_EXEC, __text"))))
#endif
int _start(const TestRunnerFunctions* hostFuncs)
{
    const void* slideBasePointers[4];
    slideBasePointers[0] = hostFuncs->basePointers[0];
    slideBasePointers[1] = hostFuncs->basePointers[1];
    slideBasePointers[2] = hostFuncs->basePointers[2];
    slideBasePointers[3] = hostFuncs->basePointers[3];
    int slideReturnCode = slide(hostFuncs->mhs[0], slideBasePointers, hostFuncs->printf);
    if ( slideReturnCode != 0 ) {
        FAIL("slide = %d\n", slideReturnCode);
        return 0;
    }

    int slideClassicReturnCode = slideClassic(hostFuncs->mhs[0], hostFuncs->printf);
    if ( slideClassicReturnCode != 0 ) {
        FAIL("mhs[0] slide classic = %d\n", slideClassicReturnCode);
        return 0;
    }

    LOG("Done sliding");

    if ( g[0] != x ) {
    	FAIL("g[0] != x, %d != %d\n", g[0], x);
    	return 0;
    }

    PASS("Success");
    return 0;
}


