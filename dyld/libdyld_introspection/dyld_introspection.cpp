/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2016 Apple Inc. All rights reserved.
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

#include <TargetConditionals.h>

#include "dyld_introspection.h"
#include "dyld_cache_format.h"
#include "ProcessAtlas.h"
#include "MachOLoaded.h"

#define NO_ULEB
#include "FileAbstraction.hpp"
#include "MachOFileAbstraction.hpp"

using namespace dyld3;
using namespace dyld4;

// This file is essentially glue to bind the public API/SPI to the internal object representations.
// No significant implementation code should be present in this file

#pragma mark -
#pragma mark Process

#if BUILDING_LIBDYLD || BUILDING_LIBDYLD_INTROSPECTION || BUILDING_UNIT_TESTS

dyld_process_t dyld_process_create_for_task(task_t task, kern_return_t *kr) {
    return (dyld_process_t)dyld4::Atlas::Process::createForTask(task, kr).release();
}

dyld_process_t dyld_process_create_for_current_task() {
    return dyld_process_create_for_task(mach_task_self(), nullptr);
}

dyld_shared_cache_t dyld_shared_cache_create(dyld_process_t process) {
    kern_return_t kr = KERN_SUCCESS;
    auto snapshot = ((dyld4::Atlas::Process*)process)->createSnapshot(&kr);
    return (dyld_shared_cache_t)snapshot->sharedCache().release();
}

void dyld_shared_cache_dispose(dyld_shared_cache_t cache) {
    UniquePtr<Atlas::SharedCache>((Atlas::SharedCache*)cache);
}

void dyld_process_dispose(dyld_process_t process) {
    UniquePtr<Atlas::Process> temp((Atlas::Process*)process);
}

#if 0
uint32_t dyld_process_register_for_image_notifications(dyld_process_t, kern_return_t *kr,
                                                       dispatch_queue_t queue, void (^block)(dyld_image_t image, bool load)) {
    //TODO: Implementation
    return 0;
}
#endif

uint32_t dyld_process_register_for_event_notification(dyld_process_t process, kern_return_t *kr, uint32_t event,
                                                      dispatch_queue_t queue, void (^block)()) {
    kern_return_t krSink = KERN_SUCCESS;
    if (kr == nullptr) {
        kr = &krSink;
    }
    return ((dyld4::Atlas::Process*)process)->registerEventHandler(kr, event, queue, block);
}

void dyld_process_unregister_for_notification(dyld_process_t process, uint32_t handle) {
    ((dyld4::Atlas::Process*)process)->unregisterEventHandler(handle);
}

#pragma mark -
#pragma mark Process Snaphsot

dyld_process_snapshot_t dyld_process_snapshot_create_for_process(dyld_process_t process, kern_return_t *kr) {
    return (dyld_process_snapshot_t)((dyld4::Atlas::Process*)process)->createSnapshot(kr).release();
}

void dyld_process_snapshot_dispose(dyld_process_snapshot_t snapshot) {
    UniquePtr<Atlas::ProcessSnapshot> temp((Atlas::ProcessSnapshot*)snapshot);
}

#if 0
void dyld_process_snapshot_for_each_image(dyld_process_snapshot_t snapshot, void (^block)(dyld_image_t image)) {
    ((dyld::Atlas::ProcessSnapshot*)snapshot)->forEachImage(^(dyld::Atlas::Image* image) {
        block((dyld_image_t)image);
    });
}
#endif

dyld_shared_cache_t dyld_process_snapshot_get_shared_cache(dyld_process_snapshot_t snapshot) {
    return (dyld_shared_cache_t)(((Atlas::ProcessSnapshot*)snapshot)->sharedCache().get());
}

#endif /* BUILDING_LIBDYLD || BUILDING_LIBDYLD_INTROSPECTION */

#pragma mark -
#pragma mark SharedCache

bool dyld_shared_cache_pin_mapping(dyld_shared_cache_t cache) {
    return ((dyld4::Atlas::SharedCache*)cache)->pin();
}

void dyld_shared_cache_unpin_mapping(dyld_shared_cache_t cache) {
    ((dyld4::Atlas::SharedCache*)cache)->unpin();
}

uint64_t dyld_shared_cache_get_base_address(dyld_shared_cache_t cache_atlas) {
    auto cache = (dyld4::Atlas::SharedCache*)cache_atlas;
    return cache->baseAddress();
}

uint64_t dyld_shared_cache_get_mapped_size(dyld_shared_cache_t cache_atlas) {
    auto cache = (dyld4::Atlas::SharedCache*)cache_atlas;
    return cache->size();
}

bool dyld_shared_cache_is_mapped_private(dyld_shared_cache_t cache_atlas) {
    auto cache = (dyld4::Atlas::SharedCache*)cache_atlas;
    return cache->isPrivateMapped();
}

void dyld_shared_cache_copy_uuid(dyld_shared_cache_t cache_atlas, uuid_t *uuid) {
    auto cache = (dyld4::Atlas::SharedCache*)cache_atlas;
    memcpy((void*)&uuid[0], cache->uuid().begin(), 16);
}

void dyld_shared_cache_for_each_file(dyld_shared_cache_t cache_atlas, void (^block)(const char* file_path)) {
    auto cache = (dyld4::Atlas::SharedCache*)cache_atlas;
    cache->forEachFilePath(block);
}

void dyld_shared_cache_for_each_image(dyld_shared_cache_t cache, void (^block)(dyld_image_t image)) {
    ((dyld4::Atlas::SharedCache*)cache)->forEachImage(^(dyld4::Atlas::Image* image) {
        block((dyld_image_t)image);
    });
}

void dyld_for_each_installed_shared_cache_with_system_path(const char* root_path, void (^block)(dyld_shared_cache_t atlas)) {
    //FIXME: We should pass through root_path instead of "/", but this is a workaround for rdar://76615959
    dyld4::Atlas::SharedCache::forEachInstalledCacheWithSystemPath("/", ^(dyld4::Atlas::SharedCache* cache){
        block((dyld_shared_cache_t)cache);
    });
}

void dyld_for_each_installed_shared_cache(void (^block)(dyld_shared_cache_t cache)) {
    dyld4::Atlas::SharedCache::forEachInstalledCacheWithSystemPath("/", ^(dyld4::Atlas::SharedCache* cache){
        block((dyld_shared_cache_t)cache);
    });
}

extern bool dyld_shared_cache_for_file(const char* filePath, void (^block)(dyld_shared_cache_t cache)) {
    auto cache = dyld4::Atlas::SharedCache::createForFilePath(filePath);
    if (cache) {
        block((dyld_shared_cache_t)cache.get());
        return true;
    }
    return false;
}

bool dyld_image_content_for_segment(dyld_image_t image, const char* segmentName,
                                    void (^contentReader)(const void* content, uint64_t vmAddr, uint64_t vmSize)) {
    return ((dyld4::Atlas::Image*)image)->contentForSegment(segmentName, contentReader);
}

bool dyld_image_content_for_section(dyld_image_t image, const char* segmentName, const char* sectionName,
                                    void (^contentReader)(const void* content, uint64_t vmAddr, uint64_t vmSize)) {
    return ((dyld4::Atlas::Image*)image)->contentForSection(segmentName, sectionName, contentReader);
}

bool dyld_image_copy_uuid(dyld_image_t image, uuid_t* uuid) {
    DRL::UUID imageUUID = ((dyld4::Atlas::Image*)image)->uuid();
    if (imageUUID.empty()) {
        return false;
    }
    std::copy(imageUUID.begin(), imageUUID.end(), (uint8_t*)&uuid[0]);
    return true;
}

bool dyld_image_for_each_segment_info(dyld_image_t image, void (^block)(const char* segmentName, uint64_t vmAddr, uint64_t vmSize, int perm)) {
    return ((dyld4::Atlas::Image*)image)->forEachSegment(block);
}

bool dyld_image_for_each_section_info(dyld_image_t image,
                                 void (^block)(const char* segmentName, const char* sectionName, uint64_t vmAddr, uint64_t vmSize)) {
    return ((dyld4::Atlas::Image*)image)->forEachSection(block);
}

const char* dyld_image_get_installname(dyld_image_t image) {
    return ((dyld4::Atlas::Image*)image)->installname();
}

#if 0
const char* dyld_image_get_file_path(dyld_image_t image) {
    return ((dyld4::Atlas::Image*)image)->filename();
}
#endif


// FIXME: These functions are part of DyldSharedCache.cpp and we should use that, but we can't until we factor out libdyld_introspection
static
const void* getLocalNlistEntries(const dyld_cache_local_symbols_info* localInfo) {
    return (uint8_t*)localInfo + localInfo->nlistOffset;
}

static
const char* getLocalStrings(const dyld_cache_local_symbols_info* localInfo)
{
    return (char*)localInfo + localInfo->stringsOffset;
}

static
void forEachLocalSymbolEntry(const dyld_cache_local_symbols_info* localInfo,
                             bool use64BitDylibOffsets,
                             void (^handler)(uint64_t dylibCacheVMOffset, uint32_t nlistStartIndex, uint32_t nlistCount, bool& stop))
{
    if ( use64BitDylibOffsets ) {
        // On new caches, the dylibOffset is 64-bits, and is a VM offset
        const auto* localEntries = (dyld_cache_local_symbols_entry_64*)((uint8_t*)localInfo + localInfo->entriesOffset);
        bool stop = false;
        for (uint32_t i = 0; i < localInfo->entriesCount; i++) {
            const dyld_cache_local_symbols_entry_64& localEntry = localEntries[i];
            handler(localEntry.dylibOffset, localEntry.nlistStartIndex, localEntry.nlistCount, stop);
            if ( stop )
                break;
        }
    } else {
        // On old caches, the dylibOffset is 64-bits, and is a file offset
        // Note, as we are only looking for mach_header's, a file offset is a VM offset in this case
        const auto* localEntries = (dyld_cache_local_symbols_entry*)((uint8_t*)localInfo + localInfo->entriesOffset);
        bool stop = false;
        for (uint32_t i = 0; i < localInfo->entriesCount; i++) {
            const dyld_cache_local_symbols_entry& localEntry = localEntries[i];
            handler(localEntry.dylibOffset, localEntry.nlistStartIndex, localEntry.nlistCount, stop);
            if ( stop )
                break;
        }
    }
}


bool dyld_image_local_nlist_content_4Symbolication(dyld_image_t image,
                                                   void (^contentReader)(const void* nListStart, uint64_t nListCount,
                                                                         const char* stringTable))
{
    dyld4::Atlas::Image* atlasImage = (dyld4::Atlas::Image*)image;
    const dyld4::Atlas::SharedCache* sharedCache = atlasImage->sharedCache();
    if ( sharedCache == nullptr )
        return false;

    if ( auto localsFileData = sharedCache->localSymbols() ) {
        uint64_t textOffsetInCache = atlasImage->sharedCacheVMOffset();

        const dyld_cache_local_symbols_info* localInfo = localsFileData->localInfo();
        forEachLocalSymbolEntry(localInfo, localsFileData->use64BitDylibOffsets(),
                                ^(uint64_t dylibCacheVMOffset, uint32_t nlistStartIndex, uint32_t nlistCount, bool& stop){
            if ( dylibCacheVMOffset == textOffsetInCache ) {
                if ( atlasImage->pointerSize() == 8 ) {
                    typedef Pointer64<LittleEndian> P;
                    const macho_nlist<P>* allLocalNlists = (macho_nlist<P>*)getLocalNlistEntries(localInfo);
                    const macho_nlist<P>* dylibNListsStart = &allLocalNlists[nlistStartIndex];
                    contentReader(dylibNListsStart, nlistCount, getLocalStrings(localInfo));
                } else {
                    typedef Pointer32<LittleEndian> P;
                    const macho_nlist<P>* allLocalNlists = (macho_nlist<P>*)getLocalNlistEntries(localInfo);
                    const macho_nlist<P>* dylibNListsStart = &allLocalNlists[nlistStartIndex];
                    contentReader(dylibNListsStart, nlistCount, getLocalStrings(localInfo));
                }
                stop = true;
            }
        });
        return true;
    }
    return true;
}
