/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2003-2010 Apple Inc. All rights reserved.
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
#ifndef _CACHE_PATCHING_H_
#define _CACHE_PATCHING_H_

#include "MachOLoaded.h"

//
// MARK: --- V1 patching.  This is for old caches, before Large/Split caches ---
//

struct dyld_cache_patch_info_v1
{
    uint64_t    patchTableArrayAddr;    // (unslid) address of array for dyld_cache_image_patches for each image
    uint64_t    patchTableArrayCount;   // count of patch table entries
    uint64_t    patchExportArrayAddr;   // (unslid) address of array for patch exports for each image
    uint64_t    patchExportArrayCount;  // count of patch exports entries
    uint64_t    patchLocationArrayAddr; // (unslid) address of array for patch locations for each patch
    uint64_t    patchLocationArrayCount;// count of patch location entries
    uint64_t    patchExportNamesAddr;   // blob of strings of export names for patches
    uint64_t    patchExportNamesSize;   // size of string blob of export names for patches
};

struct dyld_cache_image_patches_v1
{
    uint32_t    patchExportsStartIndex;
    uint32_t    patchExportsCount;
};

struct dyld_cache_patchable_export_v1
{
    uint32_t            cacheOffsetOfImpl;
    uint32_t            patchLocationsStartIndex;
    uint32_t            patchLocationsCount;
    uint32_t            exportNameOffset;
};

struct dyld_cache_patchable_location_v1
{
    uint32_t            cacheOffset;
    uint64_t            high7                   : 7,
                        addend                  : 5,    // 0..31
                        authenticated           : 1,
                        usesAddressDiversity    : 1,
                        key                     : 2,
                        discriminator           : 16;

    uint64_t getAddend() const {
        uint64_t unsingedAddend = addend;
        int64_t signedAddend = (int64_t)unsingedAddend;
        signedAddend = (signedAddend << 52) >> 52;
        return (uint64_t)signedAddend;
    }
};

//
// MARK: --- V2 patching.  This is for Large/Split caches and newer ---
//
struct dyld_cache_patch_info_v2
{
    uint32_t    patchTableVersion;              // == 2
    uint32_t    patchLocationVersion;           // == 0 for now
    uint64_t    patchTableArrayAddr;            // (unslid) address of array for dyld_cache_image_patches_v2 for each image
    uint64_t    patchTableArrayCount;           // count of patch table entries
    uint64_t    patchImageExportsArrayAddr;     // (unslid) address of array for dyld_cache_image_export_v2 for each image
    uint64_t    patchImageExportsArrayCount;    // count of patch table entries
    uint64_t    patchClientsArrayAddr;          // (unslid) address of array for dyld_cache_image_clients_v2 for each image
    uint64_t    patchClientsArrayCount;         // count of patch clients entries
    uint64_t    patchClientExportsArrayAddr;    // (unslid) address of array for patch exports for each client image
    uint64_t    patchClientExportsArrayCount;   // count of patch exports entries
    uint64_t    patchLocationArrayAddr;         // (unslid) address of array for patch locations for each patch
    uint64_t    patchLocationArrayCount;        // count of patch location entries
    uint64_t    patchExportNamesAddr;           // blob of strings of export names for patches
    uint64_t    patchExportNamesSize;           // size of string blob of export names for patches
};

struct dyld_cache_image_patches_v2
{
    uint32_t    patchClientsStartIndex;
    uint32_t    patchClientsCount;
    uint32_t    patchExportsStartIndex;         // Points to dyld_cache_image_export_v2[]
    uint32_t    patchExportsCount;
};

struct dyld_cache_image_export_v2
{
    uint32_t    dylibOffsetOfImpl;              // Offset from the dylib we used to find a dyld_cache_image_patches_v2
    uint32_t    exportNameOffset;
};

struct dyld_cache_image_clients_v2
{
    uint32_t    clientDylibIndex;
    uint32_t    patchExportsStartIndex;         // Points to dyld_cache_patchable_export_v2[]
    uint32_t    patchExportsCount;
};

struct dyld_cache_patchable_export_v2
{
    uint32_t    imageExportIndex;               // Points to dyld_cache_image_export_v2
    uint32_t    patchLocationsStartIndex;       // Points to dyld_cache_patchable_location_v2[]
    uint32_t    patchLocationsCount;
};

struct dyld_cache_patchable_location_v2
{
    uint32_t    dylibOffsetOfUse;               // Offset from the dylib we used to get a dyld_cache_image_clients_v2
    uint32_t    high7                   : 7,
                addend                  : 5,    // 0..31
                authenticated           : 1,
                usesAddressDiversity    : 1,
                key                     : 2,
                discriminator           : 16;

    uint64_t getAddend() const {
        uint64_t unsingedAddend = addend;
        int64_t signedAddend = (int64_t)unsingedAddend;
        signedAddend = (signedAddend << 52) >> 52;
        return (uint64_t)signedAddend;
    }
};

#endif /* _CACHE_PATCHING_H_ */

