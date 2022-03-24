#!/usr/bin/python2.7

import os
import KernelCollection

# Verify that we fail to generate FIPS hash as the section isn't large enough

def check(kernel_cache):
    kernel_cache.buildKernelCollection("arm64", "/fips-31/main.kc", "/fips-31/main.kernel", "/fips-31/extensions", ["com.apple.kec.corecrypto"], [])
    kernel_cache.analyze("/fips-31/main.kc", ["-layout", "-arch", "arm64"])

    assert len(kernel_cache.dictionary()["dylibs"]) == 2
    assert kernel_cache.dictionary()["dylibs"][0]["name"] == "com.apple.kernel"
    assert kernel_cache.dictionary()["dylibs"][1]["name"] == "com.apple.kec.corecrypto"

    # Check fips
    kernel_cache.analyze("/fips-31/main.kc", ["-fips", "-arch", "arm64"])
    # The whole 31-bytes are the original value, as we failed to measure it
    assert kernel_cache.dictionary()["fips"] == "01010101010101010101010101010101010101010101010101010101010101"

# [~]> xcrun -sdk iphoneos cc -arch arm64 -Wl,-static -mkernel -nostdlib -Wl,-add_split_seg_info -Wl,-rename_section,__TEXT,__text,__TEXT_EXEC,__text -Wl,-e,__start -Wl,-pagezero_size,0x0 -Wl,-pie main.c -o main.kernel
# [~]> xcrun -sdk iphoneos cc -arch arm64 -Wl,-kext -mkernel -nostdlib -Wl,-add_split_seg_info -Wl,-no_data_const foo.c -o extensions/foo.kext/foo
# [~]> rm -r extensions/*.kext/*.ld

