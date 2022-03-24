/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2004-2009 Apple Inc. All rights reserved.
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

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mach-o/loader.h>
#include <mach-o/dyld_images.h>
#include <mach-o/dyld_process_info.h>
#include <mach/mach_time.h>

#include "Vector.h"
#include "Tracing.h"
#include "Array.h"
#include "DebuggerSupport.h"
#include "MachOFile.h"

extern void* __dso_handle;

// lldb sets a break point on this function
extern "C" 	void _dyld_debugger_notification(enum dyld_notify_mode mode, unsigned long count, uint64_t machHeaders[]);

namespace dyld4 {

static Vector<dyld_image_info>*     sImageInfos    = nullptr;
static Vector<dyld_uuid_info>*      sImageUUIDs    = nullptr;
#if SUPPORT_ROSETTA
static Vector<dyld_aot_image_info>* sAotImageInfos = nullptr;
#endif


void addNonSharedCacheImageUUID(Allocator& allocator, const dyld_uuid_info& info)
{
	// set uuidArray to NULL to denote it is in-use
	gProcessInfo->uuidArray = NULL;

	// append all new images
    if ( sImageUUIDs == nullptr )
        sImageUUIDs = Vector<dyld_uuid_info>::make(allocator);
	sImageUUIDs->push_back(info);
	gProcessInfo->uuidArrayCount = sImageUUIDs->size();

	// set uuidArray back to base address of vector (other process can now read)
	gProcessInfo->uuidArray = sImageUUIDs->begin();
}

void addImagesToAllImages(Allocator& allocator, uint32_t infoCount, const dyld_image_info info[])
{
	// set infoArray to NULL to denote it is in-use
	gProcessInfo->infoArray = NULL;
	
	// append all new images
    if ( sImageInfos == nullptr ) {
        sImageInfos = Vector<dyld_image_info>::make(allocator);
        sImageInfos->reserve(256);
    }
	for (uint32_t i=0; i < infoCount; ++i)
		sImageInfos->push_back(info[i]);
	gProcessInfo->infoArrayCount = (uint32_t)sImageInfos->size();
	gProcessInfo->infoArrayChangeTimestamp = mach_absolute_time();

	// set infoArray back to base address of vector (other process can now read)
	gProcessInfo->infoArray = sImageInfos->begin();
}

#if SUPPORT_ROSETTA
void addAotImagesToAllAotImages(Allocator& allocator, uint32_t aotInfoCount, const dyld_aot_image_info aotInfo[])
{
    // rdar://74693049 (handle if aot_get_runtime_info() returns aot_image_count==0)
    if ( aotInfoCount == 0 )
        return;

    // set aotInfoArray to NULL to denote it is in-use
    gProcessInfo->aotInfoArray = NULL;

    if ( sAotImageInfos == nullptr ) {
        sAotImageInfos = Vector<dyld_aot_image_info>::make(allocator);
        sAotImageInfos->reserve(256);
    }
    for (uint32_t i = 0; i < aotInfoCount; ++i) {
        sAotImageInfos->push_back(aotInfo[i]);
    }
    gProcessInfo->aotInfoCount = (uint32_t)sAotImageInfos->size();
    gProcessInfo->aotInfoArrayChangeTimestamp = mach_absolute_time();

    // set aotInfoArray back to base address of vector (other process can now read)
    gProcessInfo->aotInfoArray = sAotImageInfos->begin();
}
#endif

#if TARGET_OS_SIMULATOR
// called once during dyld_sim start up, to copy image list from host dyld to sImageInfos
void syncProcessInfo(Allocator& allocator)
{
	// may want to set version field of gProcessInfo if it might be different than host
    if ( sImageInfos == nullptr ) {
        sImageInfos = Vector<dyld_image_info>::make(allocator);
        sImageInfos->reserve(256);
    }

    if ( gProcessInfo->infoArray != NULL ) {
        for (uint32_t i=0; i < gProcessInfo->infoArrayCount; ++i) {
            sImageInfos->push_back(gProcessInfo->infoArray[i]);
        }
        gProcessInfo->infoArray      = sImageInfos->begin();
        gProcessInfo->infoArrayCount = (uint32_t)sImageInfos->size();
    }

	gProcessInfo->notification(dyld_image_info_change, 0, NULL);
}
#endif


void removeImageFromAllImages(const mach_header* loadAddress)
{
	dyld_image_info goingAway;
	
	// set infoArray to NULL to denote it is in-use
	gProcessInfo->infoArray = NULL;
	
	// remove image from infoArray
    for (auto it=sImageInfos->begin(); it != sImageInfos->end(); ++it) {
		if ( it->imageLoadAddress == loadAddress ) {
			goingAway = *it;
			sImageInfos->erase(it);
			break;
		}
	}
	gProcessInfo->infoArrayCount = (uint32_t)sImageInfos->size();
	
	// set infoArray back to base address of vector
	gProcessInfo->infoArray = sImageInfos->begin();

	// set uuidArrayCount to NULL to denote it is in-use
	gProcessInfo->uuidArray = NULL;
	
	// remove image from infoArray
    for (auto it=sImageUUIDs->begin(); it != sImageUUIDs->end(); ++it) {
		if ( it->imageLoadAddress == loadAddress ) {
			sImageUUIDs->erase(it);
			break;
		}
	}
	gProcessInfo->uuidArrayCount = sImageUUIDs->size();
	gProcessInfo->infoArrayChangeTimestamp = mach_absolute_time();

	// set infoArray back to base address of vector
	gProcessInfo->uuidArray = sImageUUIDs->begin();

	// tell gdb that about the new images
	gProcessInfo->notification(dyld_image_removing, 1, &goingAway);
}

} // namespace dyld4

#if TARGET_OS_SIMULATOR
    struct dyld_all_image_infos* gProcessInfo = nullptr;
#else

	static void lldb_image_notifier(enum dyld_image_mode mode, uint32_t infoCount, const dyld_image_info info[])
	{
#if BUILDING_DYLD
		dyld3::ScopedTimer(DBG_DYLD_GDB_IMAGE_NOTIFIER, 0, 0, 0);
		uint64_t machHeaders[infoCount];
		for (uint32_t i=0; i < infoCount; ++i) {
			machHeaders[i] = (uintptr_t)(info[i].imageLoadAddress);
		}
    #if BUILDING_DYLD
		switch ( mode ) {
			 case dyld_image_adding:
				_dyld_debugger_notification(dyld_notify_adding, infoCount, machHeaders);
				break;
			 case dyld_image_removing:
				_dyld_debugger_notification(dyld_notify_removing, infoCount, machHeaders);
				break;
			default:
				break;
		}
    #endif
		// do nothing
		// gdb sets a break point here to catch notifications
		//dyld::log("dyld: lldb_image_notifier(%s, %d, ...)\n", mode ? "dyld_image_removing" : "dyld_image_adding", infoCount);
		//for (uint32_t i=0; i < infoCount; ++i)
		//	dyld::log("dyld: %d loading at %p %s\n", i, info[i].imageLoadAddress, info[i].imageFilePath);
		//for (uint32_t i=0; i < gProcessInfo->infoArrayCount; ++i)
		//	dyld::log("dyld: %d loading at %p %s\n", i, gProcessInfo->infoArray[i].imageLoadAddress, gProcessInfo->infoArray[i].imageFilePath);
#endif
	}

#define STR(s) # s
#define XSTR(s) STR(s)

#if defined(__cplusplus) && (BUILDING_LIBDYLD || BUILDING_DYLD)
    #define MAYBE_ATOMIC(x) {x}
#else
    #define MAYBE_ATOMIC(x)  x
#endif

struct dyld_all_image_infos  dyld_all_image_infos __attribute__ ((section ("__DATA,__all_image_info")))
                            = {
                                17, 0, MAYBE_ATOMIC(NULL), &lldb_image_notifier, false, false, (const mach_header*)&__dso_handle, NULL,
                                XSTR(DYLD_VERSION), NULL, 0, NULL, 0, 0, NULL, &dyld_all_image_infos,
                                0, 0, NULL, NULL, NULL, 0, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,},
                                0, MAYBE_ATOMIC(0), "/usr/lib/dyld", {0}, {0}, 0, 0, NULL, 0
                                };

struct dyld_shared_cache_ranges dyld_shared_cache_ranges;

struct dyld_all_image_infos* gProcessInfo = &dyld_all_image_infos;

#endif
