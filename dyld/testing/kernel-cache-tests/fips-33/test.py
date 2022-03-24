#!/usr/bin/python2.7

import os
import KernelCollection

# Verify that we generate FIPS hash

def check(kernel_cache):
    kernel_cache.buildKernelCollection("arm64", "/fips-33/main.kc", "/fips-33/main.kernel", "/fips-33/extensions", ["com.apple.kec.corecrypto"], [])
    kernel_cache.analyze("/fips-33/main.kc", ["-layout", "-arch", "arm64"])

    assert len(kernel_cache.dictionary()["dylibs"]) == 2
    assert kernel_cache.dictionary()["dylibs"][0]["name"] == "com.apple.kernel"
    assert kernel_cache.dictionary()["dylibs"][1]["name"] == "com.apple.kec.corecrypto"

    # Check fips
    kernel_cache.analyze("/fips-33/main.kc", ["-fips", "-arch", "arm64"])
    # The first 32-byes are the hash, 878d28ddc0d77d8581ee3c9fd442c04f6bdc1f600c3900a805edfa381c098cf0, and the 01 on the end is padding from the original kext
    assert kernel_cache.dictionary()["fips"] == "878d28ddc0d77d8581ee3c9fd442c04f6bdc1f600c3900a805edfa381c098cf001"

# [~]> xcrun -sdk iphoneos cc -arch arm64 -Wl,-static -mkernel -nostdlib -Wl,-add_split_seg_info -Wl,-rename_section,__TEXT,__text,__TEXT_EXEC,__text -Wl,-e,__start -Wl,-pagezero_size,0x0 -Wl,-pie main.c -o main.kernel
# [~]> xcrun -sdk iphoneos cc -arch arm64 -Wl,-kext -mkernel -nostdlib -Wl,-add_split_seg_info -Wl,-no_data_const foo.c -o extensions/foo.kext/foo
# [~]> rm -r extensions/*.kext/*.ld

