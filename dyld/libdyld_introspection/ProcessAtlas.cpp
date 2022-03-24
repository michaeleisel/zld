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

#include <Block.h>
#include <ctype.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <libproc.h>

#include <sys/attr.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/fsgetpath.h>

#include <mach/mach_vm.h>
#include <mach-o/dyld_priv.h> // FIXME: We can remove this once we fully integrate into dyld4
#include "dyld_cache_format.h"
//FIXME: We should remove this header
#include "dyld_process_info_internal.h" // For dyld_all_image_infos_{32,64}

#include "Defines.h"
#include "MachOFile.h"
#include "ProcessAtlas.h"

#include "ProcessAtlas.h"

using namespace dyld4;
// TODO: forEach shared cache needs to filter out subcaches and skip them

namespace {
static Allocator* libDylAllocator() {
    static Allocator* allocator = nullptr;
    if (!allocator) {
        allocator = Allocator::bootstrap();
    }
    return allocator;
}

static const size_t kCachePeekSize = 0x4000;

static const dyld_cache_header* cacheFilePeek(int fd, uint8_t* firstPage) {
    // sanity check header
    if ( pread(fd, firstPage, kCachePeekSize, 0) != kCachePeekSize ) {
        return nullptr;
    }
    const dyld_cache_header* cache = (dyld_cache_header*)firstPage;
    if ( strncmp(cache->magic, "dyld_v1", strlen("dyld_v1")) != 0 ) {
        return nullptr;
    }
    return cache;
}

static void getCacheInfo(const dyld_cache_header *cache, uint64_t &headerSize, bool& splitCache) {
    // If we have sub caches, then the cache header itself tells us how much space we need to cover all caches
    if ( cache->mappingOffset >= __offsetof(dyld_cache_header, subCacheArrayCount) ) {
        // New style cache
        headerSize = cache->subCacheArrayOffset + (sizeof(dyld_subcache_entry)*cache->subCacheArrayCount);
        splitCache = true;
    } else {
        // Old style cache
        headerSize = cache->imagesOffsetOld + (sizeof(dyld_cache_image_info)*cache->imagesCountOld);
        splitCache = false;
    }
}
}

namespace dyld4 {
namespace Atlas {

#pragma mark -
#pragma mark Mappers


static
void printMapping(dyld_cache_mapping_and_slide_info* mapping, uint8_t index, uint64_t slide) {
#if 0
    const char* mappingName = "*unknown*";
    if ( mapping->maxProt & VM_PROT_EXECUTE ) {
        mappingName = "__TEXT";
    } else if ( mapping->maxProt & VM_PROT_WRITE ) {
        if ( mapping->flags & DYLD_CACHE_MAPPING_AUTH_DATA ) {
            if ( mapping->flags & DYLD_CACHE_MAPPING_DIRTY_DATA )
                mappingName = "__AUTH_DIRTY";
            else if ( mapping->flags & DYLD_CACHE_MAPPING_CONST_DATA )
                mappingName = "__AUTH_CONST";
            else
                mappingName = "__AUTH";
        } else {
            if ( mapping->flags & DYLD_CACHE_MAPPING_DIRTY_DATA )
                mappingName = "__DATA_DIRTY";
            else if ( mapping->flags & DYLD_CACHE_MAPPING_CONST_DATA )
                mappingName = "__DATA_CONST";
            else
                mappingName = "__DATA";
        }
    }
    else if ( mapping->maxProt & VM_PROT_READ ) {
        mappingName = "__LINKEDIT";
    }

    fprintf(stderr, "%16s %4lluMB,  file offset: #%u/0x%08llX -> 0x%08llX,  address: 0x%08llX -> 0x%08llX\n",
            mappingName, mapping->size / (1024*1024), index, mapping->fileOffset,
            mapping->fileOffset + mapping->size, mapping->address + slide,
            mapping->address + mapping->size + slide);
#endif
}

SharedPtr<Mapper> Mapper::mapperForSharedCache(const char* cachePath, const DRL::UUID& uuid, const void* baseAddress) {
    bool        useLocalCache   = false;
    size_t      length          = 0;
    uint64_t    slide     = 0;
    int fd = open(cachePath, O_RDONLY);
    if ( fd == -1 ) {
        return nullptr;
    }
    const void* localBaseAddress = _dyld_get_shared_cache_range(&length);
    if (localBaseAddress) {
        auto localCacheHeader = ((dyld_cache_header*)localBaseAddress);
        auto localUUID = DRL::UUID(&localCacheHeader->uuid[0]);
        if (localUUID == uuid) {
            useLocalCache = true;
        }
    }
    uint8_t firstPage[kCachePeekSize];
    const dyld_cache_header* onDiskCacheHeader = cacheFilePeek(fd, &firstPage[0]);
    if (!onDiskCacheHeader) {
        close(fd);
        return nullptr;
    }
    if (baseAddress) {
        slide = (uint64_t)baseAddress-(uint64_t)onDiskCacheHeader->sharedRegionStart;
    }
    uint64_t headerSize = 0;
    bool splitCache = false;
    getCacheInfo(onDiskCacheHeader, headerSize, splitCache);
    if (splitCache && (onDiskCacheHeader->imagesCount == 0)) {
        //This is a subcache, bail
        close(fd);
        return nullptr;
    }
    void* mapping = mmap(nullptr, (size_t)headerSize, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0);
    if (mapping == MAP_FAILED) {
        close(fd);
        return nullptr;
    }
    auto onDiskHeaderBytes = (uint8_t*)mapping;
    auto onDiskCacheMappings = (dyld_cache_mapping_and_slide_info*)&onDiskHeaderBytes[onDiskCacheHeader->mappingWithSlideOffset];
    Vector<Mapper::Mapping> mappings(libDylAllocator());
    for (auto i = 0; i < onDiskCacheHeader->mappingWithSlideCount; ++i) {
        if (useLocalCache && ((onDiskCacheMappings[i].maxProt & VM_PROT_WRITE) != VM_PROT_WRITE)) {
            // This region is immutable, use in memory version
            printMapping(&onDiskCacheMappings[i], 255, slide);
            mappings.emplace_back((Mapper::Mapping){
                .address    = onDiskCacheMappings[i].address + slide,
                .size       = onDiskCacheMappings[i].size,
                .offset     = onDiskCacheMappings[i].fileOffset,
                .fd         = -1
            });
        } else {
            printMapping(&onDiskCacheMappings[i], 0, slide);
            mappings.emplace_back((Mapper::Mapping){
                .address    = onDiskCacheMappings[i].address + slide,
                .size       = onDiskCacheMappings[i].size,
                .offset     = onDiskCacheMappings[i].fileOffset,
                .fd         = fd
            });
        }
    }
    if (splitCache) {
        auto subCaches = (dyld_subcache_entry*)&onDiskHeaderBytes[onDiskCacheHeader->subCacheArrayOffset];
        for (auto i = 0; i < onDiskCacheHeader->subCacheArrayCount; ++i) {
            char subCachePath[PATH_MAX];
            snprintf(&subCachePath[0], PATH_MAX, "%s.%u", cachePath, i+1);
            fd = open(subCachePath, O_RDONLY);
            if ( fd == -1 ) {
                break;
            }
            // TODO: We should check we have enough space, but for now just allocate a page
            uint8_t firstSubPage[kCachePeekSize];
            const dyld_cache_header* subCache = cacheFilePeek(fd, &firstSubPage[0]);
            if (!subCache) {
                close(fd);
                continue;
            }
            auto subCacheheaderBytes = (uint8_t*)subCache;
            auto subCacheMappings = (dyld_cache_mapping_and_slide_info*)&subCacheheaderBytes[subCache->mappingWithSlideOffset];

            auto onDiskSubcacheUUID = DRL::UUID(subCache->uuid);
            auto subcacheUUID = DRL::UUID(subCaches[i].uuid);
            if (subcacheUUID != onDiskSubcacheUUID) {
                //TODO: Replace this with a set
                Vector<int> fds(libDylAllocator());
                for (auto& deadMapping : mappings) {
                    if (deadMapping.fd == -1) { continue; }
                    if (std::find(fds.begin(), fds.end(), deadMapping.fd) == fds.end()) {
                        fds.push_back(deadMapping.fd);
                    }
                }
                for (auto& deadFd : fds) {
                    close(deadFd);
                }
                return nullptr;
            }

            for (auto j = 0; j < subCache->mappingWithSlideCount; ++j) {
                if (useLocalCache && ((onDiskCacheMappings[j].maxProt & VM_PROT_WRITE) != VM_PROT_WRITE)) {
                    // This region is immutable, use in memory version
                    printMapping(&subCacheMappings[j], 255, slide);;
                    mappings.emplace_back((Mapper::Mapping){
                        .address    = subCacheMappings[j].address + slide,
                        .size       = subCacheMappings[j].size,
                        .offset     = subCacheMappings[j].fileOffset,
                        .fd         = -1
                    });
                } else {
                    printMapping(&subCacheMappings[j], j+1, slide);;
                    mappings.emplace_back((Mapper::Mapping){
                        .address    = subCacheMappings[j].address + slide,
                        .size       = subCacheMappings[j].size,
                        .offset     = subCacheMappings[j].fileOffset,
                        .fd         = fd
                    });
                }
            }
        }
    }
    munmap(mapping,(size_t)headerSize);
    return SharedPtr<Mapper>(new (libDylAllocator()) Mapper(mappings));
}


std::pair<SharedPtr<Mapper>,uint64_t> Mapper::mapperForSharedCacheLocals(const char* filePath) {
    struct stat statbuf;
    if ( ::stat(filePath, &statbuf) != 0 ) {
        return { SharedPtr<Mapper>(), 0};
    }

    int fd = open(filePath, O_RDONLY);
    if ( fd == -1 ) {
        return { SharedPtr<Mapper>(), 0};
    }

    // sanity check header
    uint8_t firstPage[kCachePeekSize];
    const dyld_cache_header* cache = cacheFilePeek(fd, &firstPage[0]);
    if (!cache) {
        close(fd);
        return { SharedPtr<Mapper>(), 0};
    }
    uint64_t baseAddress = 0;

    // We want the cache header, which is at the start of the, and the locals, which are at the end.
    // Just map the whole file as a single range, as we need file offsets in the mappings anyway
    // With split caches, this is more reasonable as the locals are in their own file, so we want more or
    // less the whole file anyway, and there's no wasted space for __TEXT, __DATA, etc.
//    fprintf(stderr, "Mapping\n");
//    fprintf(stderr, "fd\tAddress\tFile Offset\tSize\n");
//    fprintf(stderr, "%u\t0x%llx\t0x%x\t%llu\n", fd, baseAddress, 0, (uint64_t)statbuf.st_size);
    Vector<Mapper::Mapping> mappings(libDylAllocator());
    mappings.emplace_back((Mapper::Mapping){
        .address = baseAddress,
        .size = (uint64_t)statbuf.st_size,
        .offset = 0,
        .fd = fd
    });
    return  {SharedPtr<Mapper>(new (libDylAllocator()) Mapper(mappings)), baseAddress};
}

Mapper::Mapper() : _mappings({{ .address = 0, .size = std::numeric_limits<uint64_t>::max(), .offset = 0, .fd = -1 }}, libDylAllocator()), _flatMapping(nullptr) {}
Mapper::Mapper(const Vector<Mapping>& M) : _mappings(M), _flatMapping(nullptr) {}

Mapper::~Mapper() {
    assert(_flatMapping == nullptr);
    //TODO: Replace this with a set
    Vector<int> fds(libDylAllocator());
    for (auto& mapping : _mappings) {
        if (mapping.fd == -1) { continue; }
        if (std::find(fds.begin(), fds.end(), mapping.fd) == fds.end()) {
            fds.push_back(mapping.fd);
        }
    }
    for (auto& fd : fds) {
        close(fd);
    }
}

std::pair<void*,bool> Mapper::map(const void* addr, uint64_t size) const {
    if (_flatMapping) {
        uint64_t offset = (uint64_t)addr-(uint64_t)baseAddress();
        return {(void*)((uintptr_t)_flatMapping+offset),false};
    }
    for (const auto& mapping : _mappings) {
        if (((uint64_t)addr >= mapping.address) && ((uint64_t)addr < (mapping.address + mapping.size))) {
            if (mapping.fd == -1) {
                return {(void*)((uint64_t)addr+mapping.offset), false};
            }
            assert(((uint64_t)addr + size) <= mapping.address + mapping.size);
            off_t offset = (off_t)addr - mapping.address + mapping.offset;
            // Handle unaligned mmap
            void* newMapping = nullptr;
            size_t extraBytes = 0;
            off_t roundedOffset = offset & (-1*PAGE_SIZE);
            extraBytes = (size_t)offset - (size_t)roundedOffset;
            newMapping = mmap(nullptr, (size_t)size+extraBytes, PROT_READ, MAP_FILE | MAP_PRIVATE, mapping.fd, roundedOffset);
            if (newMapping == (void*)-1) {
                printf("mmap failed: %s (%d)\n", strerror(errno), errno);
                return {(void*)1, false};
            }
            return {(void*)((uintptr_t)newMapping+extraBytes),true};
        }
    }
    return {(void*)-1, false};
}

void Mapper::unmap(const void* addr, uint64_t size) const {
    void* roundedAddr = (void*)((intptr_t)addr & (-1*PAGE_SIZE));
    size_t extraBytes = (uintptr_t)addr - (uintptr_t)roundedAddr;
    munmap(roundedAddr, (size_t)size+extraBytes);
}

const void* Mapper::baseAddress() const {
    return (const void*)_mappings[0].address;
}

const uint64_t Mapper::size() const {
    return (_mappings.back().address - _mappings[0].address) + _mappings.back().size;
}

bool Mapper::pin() {
    assert(_flatMapping == nullptr);
    //TODO: Move onto dyld allocators once we merge the large allocations support
    if (vm_allocate(mach_task_self(), (vm_address_t*)&_flatMapping, (vm_size_t)size(), VM_FLAGS_ANYWHERE) != KERN_SUCCESS) {
        return false;
    }
    for (const auto& mapping : _mappings) {
        uint64_t destAddr = (mapping.address - _mappings[0].address) + (uint64_t)_flatMapping;
        if (mapping.fd == -1) {
            if (vm_copy(mach_task_self(), (vm_address_t)mapping.address, (vm_size_t)mapping.size, (vm_address_t)destAddr) != KERN_SUCCESS) {
                unpin();
                return false;
            }
        } else {
            if (mmap((void*)destAddr, (vm_size_t)mapping.size, PROT_READ, MAP_FILE | MAP_PRIVATE | MAP_FIXED, mapping.fd, mapping.offset) == MAP_FAILED) {
                unpin();
                return false;
            }
        }
    }
    return true;
}
void Mapper::unpin() {
    assert(_flatMapping != nullptr);
    vm_deallocate(mach_task_self(), (vm_address_t)_flatMapping, (vm_size_t)size());
    _flatMapping = nullptr;
}


#pragma mark -
#pragma mark Image

Image::Image(SharedPtr<Mapper>& M, void* A, uint64_t S, const SharedCache* SC) : _slide(S), _address(A), _mapper(M), _sharedCache(SC) {}

const MachOLoaded* Image::ml() {
    void* slidML = (void*)((uintptr_t)_address+_slide);
    if (!_ml) {
        // Note, using 4k here as we might be an arm64e process inspecting an x86_64 image, which uses 4k pages
        _ml = _mapper->map<MachOLoaded>(slidML, 4096);
        size_t size = _ml->sizeofcmds;
        if ( _ml->magic == MH_MAGIC_64 ) {
            size += sizeof(mach_header_64);
        } else {
            size += sizeof(mach_header);
        }
        if (size > 4096) {
            _ml = _mapper->map<MachOLoaded>(slidML, size);
        }
    }
    // This is a bit of a mess. With compact info this will be unified, but for now we use a lot of hacky abstactions here to deal with
    // in process / vs out of process / vs shared cache.
    return &*_ml;
}

//const MachOLoaded*              ml();
//DRL::UUID                       _uuid;
//Mapper::Pointer<MachOLoaded>    _ml;
//const uint64_t                  _slide              = 0;
//const void*                     _address            = nullptr;
//SharedPtr<Mapper>               _mapper;
//const SharedCache*              _sharedCache        = nullptr;
//const char*                     _installname        = nullptr;
//const char*                     _filename           = nullptr;
//bool                            _uuidLoaded         = false;
//bool                            _installnameLoaded  = false;
//bool                            _filenameLoaded     = false;

const DRL::UUID& Image::uuid() {
    if (!_uuidLoaded) {
        uuid_t fileUUID;
        if (ml()->getUuid(fileUUID)) {
            _uuid = DRL::UUID(fileUUID);
        }
        _uuidLoaded = true;
    }
    return _uuid;
}

const char* Image::installname() {
    if (!_installnameLoaded) {
        _installname = ml()->installName();
        _installnameLoaded = true;
    }
    return _installname;
}
const char* Image::filename() {
    if (!_filenameLoaded) {
        //TODO: The filename can be derived via the fsid objects in all image info
        _filenameLoaded = true;
    }
    return _filename;
}

const SharedCache* Image::sharedCache() const {
    return _sharedCache;
}

uint64_t Image::sharedCacheVMOffset() const {
    return (uint64_t)_address - sharedCache()->baseAddress();
}

uint32_t Image::pointerSize() {
    return ml()->pointerSize();
}


bool Image::forEachSegment(void (^block)(const char* segmentName, uint64_t vmAddr, uint64_t vmSize, int perm)) {
    ml()->forEachSegment(^(const MachOLoaded::SegmentInfo &info, bool &stop) {
        block(info.segName, info.vmAddr + _slide, info.vmSize, info.protections);
    });
    return true;
}

bool Image::forEachSection(void (^block)(const char* segmentName, const char* sectionName, uint64_t vmAddr, uint64_t vmSize)) {
    ml()->forEachSection(^(const MachOLoaded::SectionInfo &info, bool malformedSectionRange, bool &stop) {
        block(info.segInfo.segName, info.sectName, info.sectAddr + _slide, info.sectSize);
    });
    return true;
}

bool Image::contentForSegment(const char* segmentName, void (^contentReader)(const void* content, uint64_t vmAddr, uint64_t vmSize)) {
    __block bool result = false;
    ml()->forEachSegment(^(const MachOLoaded::SegmentInfo &info, bool &stop) {
        if (strcmp(segmentName, info.segName) != 0) { return; }
        if (info.vmSize) {
            auto content = _mapper->map<uint8_t>((void*)(info.vmAddr+_slide), info.vmSize);
            contentReader((void*)&*content, info.vmAddr + _slide, info.vmSize);
        } else {
            contentReader(nullptr, info.vmAddr + _slide, 0);
        }
        result = true;
        stop = true;
    });
    return result;
}

bool Image::contentForSection(const char* segmentName, const char* sectionName,
                              void (^contentReader)(const void* content, uint64_t vmAddr, uint64_t vmSize)) {
    __block bool result = false;
    ml()->forEachSection(^(const MachOLoaded::SectionInfo &info, bool malformedRange, bool &stop) {
        if (strcmp(segmentName, info.segInfo.segName) != 0) { return; }
        if (strcmp(sectionName, info.sectName) != 0) { return; }
        if (info.sectSize) {
            auto content = _mapper->map<uint8_t>((void*)(info.sectAddr+_slide), info.sectSize);
            contentReader((void*)&*content, info.sectAddr + _slide, info.sectSize);
        } else {
            contentReader(nullptr, info.sectAddr + _slide, 0);
        }
        result = true;
        stop = true;
    });
    return result;
}

#pragma mark -
#pragma mark Shared Cache Locals

SharedCacheLocals::SharedCacheLocals(SharedPtr<Mapper>& M, bool use64BitDylibOffsets)
    : _mapper(M), _use64BitDylibOffsets(use64BitDylibOffsets) {
    auto header = _mapper->map<dyld_cache_header>((void*)0, sizeof(dyld_cache_header));

    // Map in the whole locals buffer.
    // TODO: Once we have the symbols in their own file, simplify this to just map the whole file
    // and not do the header and locals separately
    _locals = _mapper->map<uint8_t>((void*)header->localSymbolsOffset, header->localSymbolsSize);
}

const dyld_cache_local_symbols_info* SharedCacheLocals::localInfo() const {
    return (const dyld_cache_local_symbols_info*)(&*_locals);
}

bool SharedCacheLocals::use64BitDylibOffsets() const {
    return _use64BitDylibOffsets;
}

#pragma mark -
#pragma mark Shared Cache

SharedCache::SharedCache(SharedPtr<Mapper>& M, const char* FP, bool P) :
    _files(libDylAllocator()), _private(P), _images(libDylAllocator()), _mapper(M)
{
    assert(_mapper);
    auto baseAddress = _mapper->baseAddress();
    _files.emplace_back(libDylAllocator()->strdup(FP));
    _header = _mapper->map<dyld_cache_header>(baseAddress, PAGE_SIZE);
    uint64_t headerSize = 0;
    bool splitCache = false;
    getCacheInfo(&*_header, headerSize, splitCache);
    if (headerSize > PAGE_SIZE) {
        _header = _mapper->map<dyld_cache_header>(baseAddress, headerSize);
    }
    _uuid = DRL::UUID(&_header->uuid[0]);
    _slide = (uint64_t)baseAddress -  _header->sharedRegionStart;
    auto headerBytes = (uint8_t*)&*_header;
    auto mappings = (dyld_cache_mapping_and_slide_info*)&headerBytes[_header->mappingWithSlideOffset];
    uint64_t endAddress = 0;
    for (auto i = 0; i < _header->mappingWithSlideCount; ++i) {
        if (endAddress < mappings[i].address + mappings[i].size) {
            endAddress = mappings[i].address + mappings[i].size;
        }
    }
    auto images = (dyld_cache_image_info*)&headerBytes[_header->imagesOffsetOld];
    uint32_t imagesCount = _header->imagesCountOld;
    if ( _header->mappingOffset >= __offsetof(dyld_cache_header, imagesCount) ) {
        images = (dyld_cache_image_info*)&headerBytes[_header->imagesOffset];
        imagesCount = _header->imagesCount;
    }
    for (auto i = 0; i < imagesCount; ++i) {
        _images.emplace_back(libDylAllocator()->makeUnique<Image>(_mapper, (void*)images[i].address, _slide, this));
    }

    if (splitCache) {
        char cachePath[PATH_MAX];
        auto subCaches = (dyld_subcache_entry*)&headerBytes[_header->subCacheArrayOffset];
        for (auto i = 0; i < _header->subCacheArrayCount; ++i) {
            auto subCacheHeader = _mapper->map<dyld_cache_header>((void*)(subCaches[i].cacheVMOffset + (uint64_t)baseAddress), PAGE_SIZE);
            uint64_t subCacheHeaderSize = 0;
            bool splitCacheUnused;
            getCacheInfo(&*subCacheHeader, subCacheHeaderSize, splitCacheUnused);
            if (subCacheHeaderSize > PAGE_SIZE) {
                subCacheHeader = _mapper->map<dyld_cache_header>((void*)(subCaches[i].cacheVMOffset + (uint64_t)baseAddress), subCacheHeaderSize);
            }
            auto subCacheHeaderBytes = (uint8_t*)&*subCacheHeader;
            auto subCacheMappings = (dyld_cache_mapping_and_slide_info*)&subCacheHeaderBytes[subCacheHeader->mappingWithSlideOffset];
            for (auto j = 0; j < subCacheHeader->mappingWithSlideCount; ++j) {
                if (endAddress < subCacheMappings[j].address + subCacheMappings[j].size) {
                    endAddress = subCacheMappings[j].address + subCacheMappings[j].size;
                }
            }
            snprintf(&cachePath[0], PATH_MAX, "%s.%u", &*_files[0], i+1);
            _files.emplace_back(libDylAllocator()->strdup(cachePath));
        }
        if ( (_header->mappingOffset >= __offsetof(dyld_cache_header, symbolFileUUID)) && !uuid_is_null(_header->symbolFileUUID) ) {
            strlcpy(&cachePath[0], &*_files[0], PATH_MAX);
            // On new caches, the locals come from a new subCache file
            if (strstr(cachePath, ".development") != nullptr) {
                cachePath[strlen(cachePath)-(strlen(".development"))] = 0;
            }
            strlcat(cachePath, ".symbols", PATH_MAX);
            _files.emplace_back(libDylAllocator()->strdup(cachePath));
        }
    }
    _size = endAddress - _header->sharedRegionStart;
}

UniquePtr<SharedCache> SharedCache::createForTask(task_read_t task, kern_return_t *kr) {
    kern_return_t krSink = KERN_SUCCESS;
    if (kr == nullptr) {
        kr = &krSink;
    }
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    task_dyld_info_data_t task_dyld_info;
    if ( task_info(task, TASK_DYLD_INFO, (task_info_t)&task_dyld_info, &count) != KERN_SUCCESS) { return nullptr; }
    //The kernel will return MACH_VM_MIN_ADDRESS for an executable that has not had dyld loaded
    if (task_dyld_info.all_image_info_addr == MACH_VM_MIN_ADDRESS) { return nullptr; }
    uint8_t remoteBuffer[16*1024];
    mach_vm_size_t readSize = 0;
    // Using mach_vm_read_overwrite because this is part of dyld. If the file is removed or the codesignature is invalid
    // then the system is broken beyond recovery anyway
    *kr = mach_vm_read_overwrite(task, task_dyld_info.all_image_info_addr, task_dyld_info.all_image_info_size,
                                 (mach_vm_address_t)&remoteBuffer[0], &readSize);
    if (*kr != KERN_SUCCESS) {
        return nullptr;
    }
    DRL::UUID uuid;
    uint64_t baseAddress                    = 0;
    uint64_t size                           = 0;
    uint64_t FSID                           = 0;
    uint64_t FSObjID                        = 0;
    bool processDetachedFromSharedRegion    = false;
    if (task_dyld_info.all_image_info_format == TASK_DYLD_ALL_IMAGE_INFO_32 ) {
        const dyld_all_image_infos_32* info = (const dyld_all_image_infos_32*)&remoteBuffer[0];
        baseAddress                     = info->sharedCacheBaseAddress;
        processDetachedFromSharedRegion = info->processDetachedFromSharedRegion;
        FSID                            = info->cacheFSID;
        FSObjID                         = info->cacheFSObjID;
    } else {
        const dyld_all_image_infos_64* info = (const dyld_all_image_infos_64*)&remoteBuffer[0];
        baseAddress                     = info->sharedCacheBaseAddress;
        processDetachedFromSharedRegion = info->processDetachedFromSharedRegion;
        FSID                            = info->cacheFSID;
        FSObjID                         = info->cacheFSObjID;
    }
    *kr = mach_vm_read_overwrite(task, baseAddress, 16*1024, (mach_vm_address_t)&remoteBuffer, &readSize);
    if (*kr != KERN_SUCCESS) {
        return nullptr;
    }
    auto header = ((dyld_cache_header*)&remoteBuffer[0]);
    uuid = DRL::UUID(&header->uuid[0]);
    for(auto i = 0; i < header->mappingCount; ++i) {
        auto mapping = (dyld_cache_mapping_info*)&remoteBuffer[header->mappingOffset+(i*sizeof(dyld_cache_mapping_info))];
        uint64_t regionEndSize = mapping->address + mapping->size - header->sharedRegionStart;
        if (size < regionEndSize) {
            size = regionEndSize;
        }
    }
    char cachePath[PATH_MAX];

    if (FSID && FSObjID) {
        // Some older dyld_sims do not set processDetachedFromSharedRegion, so check for the presence of path info and use it if present
        if (fsgetpath(cachePath, PATH_MAX, (fsid_t*)&FSID, FSObjID) == -1) {
            *kr = KERN_FAILURE;
            return nullptr;
        }
    } else {
        if (header->platform == PLATFORM_DRIVERKIT) {
            strlcpy(cachePath, DRIVERKIT_DYLD_SHARED_CACHE_DIR, PATH_MAX);
        } else if constexpr(TARGET_OS_IPHONE) {
            strlcpy(cachePath, IPHONE_DYLD_SHARED_CACHE_DIR, PATH_MAX);
        } else {
            strlcpy(cachePath, MACOSX_MRM_DYLD_SHARED_CACHE_DIR, PATH_MAX);
        }

        if ( strcmp(header->magic, "dyld_v1  x86_64") == 0 ) {
            strlcat(cachePath, "dyld_shared_cache_x86_64", PATH_MAX);
        } else if ( strcmp(header->magic, "dyld_v1 x86_64h") == 0 ) {
            strlcat(cachePath, "dyld_shared_cache_x86_64h", PATH_MAX);
        } else if ( strcmp(header->magic, "dyld_v1  arm64e") == 0 ) {
            strlcat(cachePath, "dyld_shared_cache_arm64e", PATH_MAX);
        } else if ( strcmp(header->magic, "dyld_v1   arm64") == 0 ) {
            strlcat(cachePath, "dyld_shared_cache_arm64", PATH_MAX);
        } else if ( strcmp(header->magic, "dyld_v1  armv7k") == 0 ) {
            strlcat(cachePath, "dyld_shared_cache_armv7k", PATH_MAX);
        } else if ( strcmp(header->magic, "dyld_v1arm64_32") == 0 ) {
            strlcat(cachePath, "dyld_shared_cache_arm64_32", PATH_MAX);
        }
        if constexpr(TARGET_OS_IPHONE) {
            if (header->cacheType == 0 && header->platform != PLATFORM_DRIVERKIT) {
                strlcat(cachePath, ".development", PATH_MAX);
            }
        }
    }

    // Use placement new since operator new is not available
    // TODO: We open the files to make the mapper and then again to find the paths, this can be made more efficient
    auto mapper = Mapper::mapperForSharedCache(cachePath, uuid, (void*)baseAddress);
    char pathBuffer[PATH_MAX];
    if (mapper && realpath(cachePath, &pathBuffer[0])) {
        return libDylAllocator()->makeUnique<SharedCache>(mapper, pathBuffer, processDetachedFromSharedRegion);
    }
    *kr = KERN_FAILURE;
    return nullptr;
}

static bool isSubCachePath(const char* path)
{
    size_t pathLen = strnlen(path, PATH_MAX);
    return ((pathLen > 1) && (path[pathLen-2] == '.') && isdigit(path[pathLen-1]));
}

void SharedCache::forEachInstalledCacheWithSystemPath(const char* systemPath, void (^block)(SharedCache* cache)) {
    // TODO: We can make this more resilient by encoding all the paths in a special section /usr/lib/dyld, and then parsing them out

    static const char* cacheDirPaths[] = {
#if TARGET_OS_IPHONE
        IPHONE_DYLD_SHARED_CACHE_DIR,
#else
        MACOSX_MRM_DYLD_SHARED_CACHE_DIR,
#endif
        DRIVERKIT_DYLD_SHARED_CACHE_DIR
    };
    for ( int i = 0; i < sizeof(cacheDirPaths)/sizeof(char*); i++ ) {
        char systemCacheDirPath[PATH_MAX];
        strlcpy(systemCacheDirPath, systemPath, PATH_MAX);
        strlcat(systemCacheDirPath, cacheDirPaths[i], PATH_MAX);
        DIR* dirp = ::opendir(systemCacheDirPath);
        if ( dirp != NULL) {
            dirent entry;
            dirent* entp = NULL;
            char cachePath[PATH_MAX];
            while ( ::readdir_r(dirp, &entry, &entp) == 0 ) {
                if ( entp == NULL )
                    break;
                if ( entp->d_type != DT_REG )
                    continue;
                if ( strlcpy(cachePath, systemCacheDirPath, PATH_MAX) >= PATH_MAX )
                    continue;
                if ( strlcat(cachePath, entp->d_name, PATH_MAX) >= PATH_MAX )
                    continue;
                if ( isSubCachePath(cachePath) )
                    continue;
                // FIXME: The memory managemnt here is awful, fix with allocators
                auto cache = Atlas::SharedCache::createForFilePath(cachePath);
                if (cache) {
                    block(cache.get());
                }
            }
            closedir(dirp);
        }
    }
}

UniquePtr<SharedCache> SharedCache::createForFilePath(const char* filePath) {
    auto uuid = DRL::UUID();
    auto fileMapper = Mapper::mapperForSharedCache(filePath, uuid, 0);
    if (!fileMapper) { return nullptr; }
    // Use placement new since operator new is not available
    char pathBuffer[PATH_MAX];
    if (realpath(filePath, &pathBuffer[0])) {
        return libDylAllocator()->makeUnique<SharedCache>(fileMapper, pathBuffer, true);
    }
    return nullptr;
}


const DRL::UUID& SharedCache::uuid() const {
    return _uuid;
}

uint64_t SharedCache::baseAddress() const {
    return (uint64_t)_mapper->baseAddress();
}

uint64_t SharedCache::size() const {
    return _size;
}

void SharedCache::forEachFilePath(void (^block)(const char* file_path)) const {
    for (auto& file : _files) {
        block(&*file);
    }
}

bool SharedCache::isPrivateMapped() const {
    return _private;
}

void SharedCache::forEachImage(void (^block)(Image* image)) {
    for(auto& image : _images) {
        block(&*image);
    }
}

// Maps the local symbols for this shared cache.
// Locals are in an unmapped part of the file, so we have to map then in separately
UniquePtr<SharedCacheLocals> SharedCache::localSymbols() const {
    // The locals might be in their own locals file, or in the main cache file.
    // Where it is depends on the cache header
    char localSymbolsCachePath[PATH_MAX];
    strlcpy(&localSymbolsCachePath[0], &*_files[0], PATH_MAX);
    bool useSymbolsFile = (_header->mappingOffset >= __offsetof(dyld_cache_header, symbolFileUUID));
    if ( useSymbolsFile ) {
        if ( uuid_is_null(_header->symbolFileUUID) )
            return nullptr;

        // On new caches, the locals come from a new subCache file
        if (strstr(localSymbolsCachePath, ".development") != nullptr) {
            localSymbolsCachePath[strlen(localSymbolsCachePath)-(strlen(".development"))] = 0;
        }
        strlcat(localSymbolsCachePath, ".symbols", PATH_MAX);
    } else {
        if ( (_header->localSymbolsSize == 0) || (_header->localSymbolsOffset == 0) )
            return nullptr;
    }

    auto [fileMapper, baseAddress] = Mapper::mapperForSharedCacheLocals(localSymbolsCachePath);
    if (!fileMapper) { return nullptr; }
    // Use placement new since operator new is not available
    return libDylAllocator()->makeUnique<SharedCacheLocals>(fileMapper, useSymbolsFile);
}

bool SharedCache::pin() {
    return _mapper->pin();
}

void SharedCache::unpin() {
    return _mapper->unpin();
}

#ifdef TARGET_OS_OSX
bool SharedCache::mapSubCacheAndInvokeBlock(const dyld_cache_header* subCacheHeader,
                                            void (^block)(const void* cacheBuffer, size_t size)) {
    auto subCacheHeaderBytes = (uint8_t*)subCacheHeader;
    uint64_t fileSize = 0;
    for(auto i = 0; i < subCacheHeader->mappingCount; ++i) {
        auto mapping = (dyld_cache_mapping_info*)&subCacheHeaderBytes[subCacheHeader->mappingOffset+(i*sizeof(dyld_cache_mapping_info))];
        uint64_t regionEndSize = mapping->fileOffset + mapping->size;
        if (fileSize < regionEndSize) {
            fileSize = regionEndSize;
        }
    }
    vm_address_t mappedSubCache = 0;
    if (vm_allocate(mach_task_self(), &mappedSubCache, (size_t)fileSize, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) {
        return false;
    }
    for(auto i = 0; i < _header->mappingCount; ++i) {
        //    _slide = _baseAddress -  _header->sharedRegionStart;
        auto mapping = (dyld_cache_mapping_info*)&subCacheHeaderBytes[subCacheHeader->mappingOffset+(i*sizeof(dyld_cache_mapping_info))];
        auto mappingBytes = _mapper->map<uint8_t>((void*)(mapping->address - _slide), mapping->size);
        vm_copy(mach_task_self(), (vm_address_t)&*mappingBytes, (vm_size_t)mapping->size, (vm_address_t)(mappedSubCache+mapping->fileOffset));
    }
    block((void*)mappedSubCache, (size_t)fileSize);
    assert(vm_deallocate(mach_task_self(), (vm_address_t)mappedSubCache, (vm_size_t)fileSize) == KERN_SUCCESS);
    return true;
}

bool SharedCache::forEachSubcache4Rosetta(void (^block)(const void* cacheBuffer, size_t size)) {
    if (strcmp(_header->magic, "dyld_v1  x86_64") != 0) {
        return false;
    }
    uint64_t headerSize;
    bool splitCache = false;
    getCacheInfo(&*_header, headerSize, splitCache);
    mapSubCacheAndInvokeBlock(&*_header, block);
    auto headerBytes = (uint8_t*)&*_header;
    if (splitCache) {
        auto subCaches = (dyld_subcache_entry*)&headerBytes[_header->subCacheArrayOffset];
        for (auto i = 0; i < _header->subCacheArrayCount; ++i) {
            auto subCacheHeader = _mapper->map<dyld_cache_header>((void*)(baseAddress() + subCaches[i].cacheVMOffset), PAGE_SIZE);
            uint64_t subCacheHeaderSize = subCacheHeader->mappingOffset+subCacheHeader->mappingCount*sizeof(dyld_cache_mapping_info);
            getCacheInfo(&*_header, headerSize, splitCache);
            if (subCacheHeaderSize > PAGE_SIZE) {
                subCacheHeader = _mapper->map<dyld_cache_header>((void*)(baseAddress() + subCaches[i].cacheVMOffset), subCacheHeaderSize);
            }
//            printf("Subcache Offset: %lx\n", (uintptr_t)&headerBytes[subCaches[i].cacheVMOffset]);
//            printf("subCacheHeader: %lx\n", (uintptr_t)&*subCacheHeader);
//            printf("Subcache magic: %s\n", subCacheHeader->magic);
            mapSubCacheAndInvokeBlock(&*subCacheHeader, block);
        }
    }
    return true;
}
#endif

#if BUILDING_LIBDYLD_INTROSPECTION || BUILDING_LIBDYLD || BUILDING_UNIT_TESTS
#pragma mark -
#pragma mark Process

Process::Process(task_read_t task, kern_return_t *kr) : _task(task),
                                                        _queue(dispatch_queue_create("com.apple.dyld.introspection", NULL)),
                                                        _registeredNotifiers(libDylAllocator())  {}

Process::~Process() {
    dispatch_async_and_wait(_queue, ^{
        if (_state == Connected) {
            teardownNotifications();
        }
    });
    dispatch_release(_queue);
}

UniquePtr<Process> Process::createForCurrentTask() {
    //FIXME: We should special case this when we do full process info
    return createForTask(mach_task_self(), nullptr);
}

UniquePtr<Process> Process::createForTask(task_read_t task, kern_return_t *kr) {
    return libDylAllocator()->makeUnique<Process>(task, kr);
}

void Process::setupNotifications(kern_return_t *kr) {
    assert(kr != NULL);
    assert(_state == Disconnected);
    // Allocate a port to listen on in this monitoring task
    mach_port_options_t options = { .flags = MPO_IMPORTANCE_RECEIVER | MPO_CONTEXT_AS_GUARD | MPO_STRICT, .mpl = { MACH_PORT_QLIMIT_DEFAULT }};
    *kr = mach_port_construct(mach_task_self(), &options, (mach_port_context_t)this, &_port);
    if (*kr != KERN_SUCCESS) {
        return;
    }
    // Setup notifications in case the send goes away
    mach_port_t previous = MACH_PORT_NULL;
    *kr = mach_port_request_notification(mach_task_self(), _port, MACH_NOTIFY_NO_SENDERS, 1, _port, MACH_MSG_TYPE_MAKE_SEND_ONCE, &previous);
    if ((*kr != KERN_SUCCESS) || previous != MACH_PORT_NULL) {
        (void)mach_port_destruct(mach_task_self(), _port, 0, (mach_port_context_t)this);
        return;
    }
    if constexpr(TARGET_OS_SIMULATOR) {
        static dispatch_once_t onceToken;
        static kern_return_t (*tdpinr)(task_t, mach_port_t) = nullptr;
        dispatch_once(&onceToken, ^{
            tdpinr = (kern_return_t (*)(task_t, mach_port_t))dlsym(RTLD_DEFAULT, "task_dyld_process_info_notify_register");
        });
        if (tdpinr) {
            *kr = tdpinr(_task, _port);
        } else {
            // We can fail silently here. It is a new SPI no one is using, and the new simulators will only supported on macOS's new enough
            // to have the task_dyld_process_info_notify_register(). The only reason not to abort() is internal developers who might accidentally
            // hit this as we transition.
            (void)mach_port_destruct(mach_task_self(), _port, 0, (mach_port_context_t)this);
            return;
        }
    } else {
        *kr = task_dyld_process_info_notify_register(_task, _port);
    }
    if (*kr != KERN_SUCCESS) {
        (void)mach_port_destruct(mach_task_self(), _port, 0, (mach_port_context_t)this);
        return;
    }
    _machSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_MACH_RECV, _port, 0, _queue);
    if (_machSource == nullptr) {
        (void)mach_port_destruct(mach_task_self(), _port, 0, (mach_port_context_t)this);
        return;
    }
    dispatch_source_set_event_handler(_machSource, ^{ handleNotifications(); });
    // Copy these into locals so the block captures them as const instead of implicitly referring to the members via this
    task_read_t blockTask = _task;
    mach_port_t blockPort = _port;
    dispatch_source_t blockSource = _machSource;
    dispatch_source_set_cancel_handler(_machSource, ^{
        if constexpr(TARGET_OS_SIMULATOR) {
            static dispatch_once_t onceToken;
            static kern_return_t (*tdpind)(task_t, mach_port_t) = nullptr;
            dispatch_once(&onceToken, ^{
                tdpind = (kern_return_t (*)(task_t, mach_port_t))dlsym(RTLD_DEFAULT, "task_dyld_process_info_notify_deregister");
            });
            if (tdpind) {
                (void)tdpind(blockTask, blockPort);
            }
        } else {
            (void)task_dyld_process_info_notify_deregister(blockTask, blockPort);
        }
        (void)mach_port_destruct(mach_task_self(), blockPort, 0, (mach_port_context_t)this);
        dispatch_release(blockSource);
    });
    dispatch_activate(_machSource);
    _state = Connected;
}

void Process::teardownNotifications() {
    assert(_state == Connected);
    if (_machSource) {
        dispatch_source_cancel(_machSource);
        _port = 0;
        _machSource = NULL;
        _state = Disconnected;
        for (auto& notiferRecord : _registeredNotifiers) {
            if (notiferRecord.notifierID != 0) {
                assert(notiferRecord.queue != NULL);
                assert(notiferRecord.block != NULL);
                dispatch_release(notiferRecord.queue);
                Block_release(notiferRecord.block);
                // Leaving a tombstone
                notiferRecord = (ProcessNotifierRecord){NULL, NULL, 0};
            }
        }
    }
}

void Process::handleNotifications() {
    if (_state != Connected) { return; }
    // This event handler block has an implicit reference to "this"
    // if incrementing the count goes to one, that means the object may have already been destroyed
    uint8_t messageBuffer[DYLD_PROCESS_INFO_NOTIFY_MAX_BUFFER_SIZE] = {};
    mach_msg_header_t* h = (mach_msg_header_t*)messageBuffer;

    kern_return_t r = mach_msg(h, MACH_RCV_MSG | MACH_RCV_VOUCHER| MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT) | MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0), 0, sizeof(messageBuffer)-sizeof(mach_msg_audit_trailer_t), _port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if ( r == KERN_SUCCESS && !(h->msgh_bits & MACH_MSGH_BITS_COMPLEX)) {
        //fprintf(stderr, "received message id=0x%X, size=%d\n", h->msgh_id, h->msgh_size);
        if ( (h->msgh_id & 0xFFFFF000) == DYLD_PROCESS_EVENT_ID_BASE ) {
            if (h->msgh_size != sizeof(mach_msg_header_t)) {
                teardownNotifications();
            } else {
                for (auto& notifier : _registeredNotifiers) {
                    if ((h->msgh_id & ~0xFFFFF000) == notifier.notifierID) {
                        dispatch_async_and_wait(notifier.queue, notifier.block);
                    }
                }
            }
            mach_msg_header_t replyHeader;
            replyHeader.msgh_bits        = MACH_MSGH_BITS_SET(MACH_MSGH_BITS_REMOTE(h->msgh_bits), 0, 0, 0);
            replyHeader.msgh_id          = 0;
            replyHeader.msgh_local_port  = MACH_PORT_NULL;
            replyHeader.msgh_remote_port  = h->msgh_remote_port;
            replyHeader.msgh_reserved    = 0;
            replyHeader.msgh_size        = sizeof(replyHeader);
            r = mach_msg(&replyHeader, MACH_SEND_MSG, replyHeader.msgh_size, 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
            if (r == KERN_SUCCESS) {
                h->msgh_remote_port = MACH_PORT_NULL;
            } else {
                teardownNotifications();
            }
        } else if ( h->msgh_id == MACH_NOTIFY_NO_SENDERS ) {
            // Validate this notification came from the kernel
            const mach_msg_audit_trailer_t *audit_tlr = (mach_msg_audit_trailer_t *)((uint8_t *)h + round_msg(h->msgh_size));
            if (audit_tlr->msgh_trailer_type == MACH_MSG_TRAILER_FORMAT_0
                && audit_tlr->msgh_trailer_size >= sizeof(mach_msg_audit_trailer_t)
                // We cannot link to libbsm, so we are hardcoding the audit token offset (5)
                // And the value the represents the kernel (0)
                && audit_tlr->msgh_audit.val[5] == 0) {
                teardownNotifications();
            }
        } else if ( h->msgh_id != DYLD_PROCESS_INFO_NOTIFY_LOAD_ID
                   && h->msgh_id != DYLD_PROCESS_INFO_NOTIFY_UNLOAD_ID
                   && h->msgh_id != DYLD_PROCESS_INFO_NOTIFY_MAIN_ID) {
            fprintf(stderr, "dyld: received unknown message id=0x%X, size=%d\n", h->msgh_id, h->msgh_size);
        }
    } else {
        fprintf(stderr, "dyld: received unknown message id=0x%X, size=%d\n", h->msgh_id, h->msgh_size);
    }
    mach_msg_destroy(h);
}


uint32_t Process::registerEventHandler(kern_return_t *kr, uint32_t event, dispatch_queue_t queue, void (^block)()) {
    __block uint32_t result = 0;
    dispatch_async_and_wait(_queue, ^{
        if (_state == Disconnected) {
            setupNotifications(kr);
            if (*kr != KERN_SUCCESS) {
                return;
            }
        }
        assert(_state == Connected);
        dispatch_retain(queue);
        _registeredNotifiers.emplace_back((ProcessNotifierRecord){queue, Block_copy(block), event});
//        fprintf(stderr, "Entered:\n");
//        for (auto i = 0 ; i < _registeredNotifiers.size(); ++i) {
//            fprintf(stderr, "%u: event %u\n", i, _registeredNotifiers[i].notifierID);
//        }
        result = (uint32_t)_registeredNotifiers.size();
//        fprintf(stderr, "result: %u\n", result);

    });
//    fprintf(stderr, "result2: %u\n", result);
    return result;
}

void Process::unregisterEventHandler(uint32_t handle) {
    dispatch_async_and_wait(_queue, ^{
//        fprintf(stderr, "Trying to remove handle (%u) from %lu\n", handle, _registeredNotifiers.size());
        assert(_registeredNotifiers.size() >= handle);
        auto& notiferRecord = _registeredNotifiers[handle-1];
        if (notiferRecord.notifierID == 0) {
            // Already torndown
            return;
        }
        assert(notiferRecord.queue != NULL);
        assert(notiferRecord.block != NULL);
        dispatch_release(notiferRecord.queue);
        Block_release(notiferRecord.block);
        // Leave a tombstone
        //FIXME: Will not be necssary if we move to Map
        notiferRecord = (ProcessNotifierRecord){NULL, NULL, 0};

        bool liveNotifiers = false;
        for (auto& notifier : _registeredNotifiers) {
            if (notifier.notifierID != 0) {
                liveNotifiers = true;
                break;
            }
        }
        if (!liveNotifiers) {
            teardownNotifications();
        }
    });
}

#pragma mark -
#pragma mark Process Snapshot

UniquePtr<ProcessSnapshot> ProcessSnapshot::createForTask(task_read_t task, kern_return_t *kr) {
    return libDylAllocator()->makeUnique<ProcessSnapshot>(task, kr);
}

ProcessSnapshot::ProcessSnapshot(task_read_t task, kern_return_t *kr) : _task(task), _sharedCache(SharedCache::createForTask(_task, nullptr)),
                                                                        _images(libDylAllocator()) {
}

void ProcessSnapshot::forEachImage(void (^block)(Image* image)) {
//    for(auto& image : _images) {
//        block(&*image);
//    }
}

UniquePtr<ProcessSnapshot> Process::createSnapshot(kern_return_t *kr) {
    return ProcessSnapshot::createForTask(_task, kr);
}

UniquePtr<SharedCache>& ProcessSnapshot::sharedCache() {
    return _sharedCache;
}
#endif /* BUILDING_LIBDYLD_INTROSPECTION || BUILDING_LIBDYLD */

};
};
