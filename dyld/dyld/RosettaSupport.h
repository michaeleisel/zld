/*
 * Copyright (c) 2020 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#ifndef RosettaSupport_h
#define RosettaSupport_h

#include <unistd.h>
#include <stdint.h>

#include <TargetConditionals.h>

#include "Defines.h"


#if SUPPORT_ROSETTA

struct dyld_all_runtime_info {
    uint64_t                    image_count;
    const dyld_image_info*      images;
    uint64_t                    uuid_count;
    const dyld_uuid_info*       uuids;
    uint64_t                    aot_image_count;
    const dyld_aot_image_info*  aots;
    dyld_aot_shared_cache_info  aot_cache_info;
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

// Called once at launch to get AOT info about main executable
inline int aot_get_runtime_info(dyld_all_runtime_info*& info)
{
    return syscall(0x7000004, &info);
}

// Called computing image size from disk to get info about translated mapping
inline int aot_get_extra_mapping_info(int fd, const char* path, uint64_t& extraAllocSize, char aotPath[], size_t aotPathSize)
{
    return syscall(0x7000001, fd, path, &extraAllocSize, aotPath, aotPathSize);
}

// Called when mmap()ing disk image, to add in translated mapping
inline int aot_map_extra(const char* path, const mach_header* mh, const void* mappingEnd, const mach_header*& aotMapping, uint64_t& aotMappingSize, uint8_t aotImageKey[32])
{
    return syscall(0x7000002, path, mh, mappingEnd, &aotMapping, &aotMappingSize, aotImageKey);
}

#pragma clang diagnostic pop



#endif /* SUPPORT_ROSETTA */

#endif /* RosettaSupport_h */
