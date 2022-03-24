/*
 * Copyright (c) 2019 Apple Inc. All rights reserved.
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

#ifndef debuggerSuppot_h
#define debuggerSuppot_h

#include <mach-o/dyld_images.h>

#include "Defines.h"
#include "Allocator.h"


namespace dyld4 {
    void addImagesToAllImages(Allocator&, uint32_t infoCount, const dyld_image_info info[]);
    void removeImageFromAllImages(const mach_header* loadAddress);
    void addNonSharedCacheImageUUID(Allocator&, const dyld_uuid_info& info);
#if TARGET_OS_SIMULATOR
    void syncProcessInfo(Allocator& allocator);
#endif

#if SUPPORT_ROSETTA
    void addAotImagesToAllAotImages(Allocator&, uint32_t aotInfoCount, const dyld_aot_image_info aotInfo[]);
#endif
}

extern dyld_all_image_infos*        gProcessInfo;

#endif /* debuggerSuppot_h */
