/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2011 Apple Inc. All rights reserved.
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syslimits.h>
#include <libkern/OSByteOrder.h>
#include <mach-o/arch.h>
#include <mach-o/loader.h>
#include <Availability.h>

#include "CodeSigningTypes.h"
#include <CommonCrypto/CommonHMAC.h>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonDigestSPI.h>

#define NO_ULEB
#include "Architectures.hpp"
#include "MachOFileAbstraction.hpp"

#include "dsc_iterator.h"
#include "dsc_extractor.h"
#include "DyldSharedCache.h"
#include "MachOAnalyzer.h"
#include "SupportedArchs.h"
#include "Trie.hpp"
#include "JSONWriter.h"
#include "StringUtils.h"

#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <dispatch/dispatch.h>
#include <optional>

static int sharedCacheIsValid(const void* mapped_cache, uint64_t size) {
    // First check that the size is good.
    // Note the shared cache may not have a codeSignatureSize value set so we need to first make
    // sure we have space for the CS_SuperBlob, then later crack that to check for the size of the rest.
    const DyldSharedCache* dyldSharedCache = (DyldSharedCache*)mapped_cache;
    uint64_t requiredSizeForCSSuperBlob = dyldSharedCache->header.codeSignatureOffset + sizeof(CS_SuperBlob);
    const dyld_cache_mapping_info* mappings = (dyld_cache_mapping_info*)((uint8_t*)mapped_cache + dyldSharedCache->header.mappingOffset);
    if ( requiredSizeForCSSuperBlob > size ) {
        fprintf(stderr, "Error: dyld shared cache size 0x%08llx is less than required size of 0x%08llx.\n", size, requiredSizeForCSSuperBlob);
        return -1;
    }

    // Now see if the code signatures are valid as that tells us the pages aren't corrupt.
    // First find all of the regions of the shared cache we computed cd hashes
    std::vector<std::pair<uint64_t, uint64_t>> sharedCacheRegions;
    for (uint32_t i = 0; i != dyldSharedCache->header.mappingCount; ++i) {
        sharedCacheRegions.emplace_back(std::make_pair(mappings[i].fileOffset, mappings[i].fileOffset + mappings[i].size));
    }
    if (dyldSharedCache->header.localSymbolsSize)
        sharedCacheRegions.emplace_back(std::make_pair(dyldSharedCache->header.localSymbolsOffset, dyldSharedCache->header.localSymbolsOffset + dyldSharedCache->header.localSymbolsSize));
    size_t inBbufferSize = 0;
    for (auto& sharedCacheRegion : sharedCacheRegions)
        inBbufferSize += (sharedCacheRegion.second - sharedCacheRegion.first);

    // Now take the cd hash from the cache itself and validate the regions we found.
    uint8_t* codeSignatureRegion = (uint8_t*)mapped_cache + dyldSharedCache->header.codeSignatureOffset;
    CS_SuperBlob* sb = reinterpret_cast<CS_SuperBlob*>(codeSignatureRegion);
    if (sb->magic != htonl(CSMAGIC_EMBEDDED_SIGNATURE)) {
        fprintf(stderr, "Error: dyld shared cache code signature magic is incorrect.\n");
        return -1;
    }

    size_t sbSize = ntohl(sb->length);
    uint64_t requiredSizeForCS = dyldSharedCache->header.codeSignatureOffset + sbSize;
    if ( requiredSizeForCS > size ) {
        fprintf(stderr, "Error: dyld shared cache size 0x%08llx is less than required size of 0x%08llx.\n", size, requiredSizeForCS);
        return -1;
    }

    // Find the offset to the code directory.
    CS_CodeDirectory* cd = nullptr;
    for (unsigned i =0; i != sb->count; ++i) {
        if (ntohl(sb->index[i].type) == CSSLOT_CODEDIRECTORY) {
            cd = (CS_CodeDirectory*)(codeSignatureRegion + ntohl(sb->index[i].offset));
            break;
        }
    }

    if (!cd) {
        fprintf(stderr, "Error: dyld shared cache code signature directory is missing.\n");
        return -1;
    }

    if ( (uint8_t*)cd > (codeSignatureRegion + sbSize) ) {
        fprintf(stderr, "Error: dyld shared cache code signature directory is out of bounds.\n");
        return -1;
    }

    if ( cd->magic != htonl(CSMAGIC_CODEDIRECTORY) ) {
        fprintf(stderr, "Error: dyld shared cache code signature directory magic is incorrect.\n");
        return -1;
    }

    uint32_t pageSize = 1 << cd->pageSize;
    uint32_t slotCountFromRegions = (uint32_t)((inBbufferSize + pageSize - 1) / pageSize);
    if ( ntohl(cd->nCodeSlots) < slotCountFromRegions ) {
        fprintf(stderr, "Error: dyld shared cache code signature directory num slots is incorrect.\n");
        return -1;
    }

    uint32_t dscDigestFormat = kCCDigestNone;
    switch (cd->hashType) {
        case CS_HASHTYPE_SHA1:
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            dscDigestFormat = kCCDigestSHA1;
#pragma clang diagnostic pop
            break;
        case CS_HASHTYPE_SHA256:
            dscDigestFormat = kCCDigestSHA256;
            break;
        default:
            break;
    }

    if (dscDigestFormat != kCCDigestNone) {
        const uint64_t csPageSize = 1 << cd->pageSize;
        size_t   hashOffset = ntohl(cd->hashOffset);
        uint8_t* hashSlot = (uint8_t*)cd + hashOffset;
        uint8_t cdHashBuffer[cd->hashSize];

        // Skip local symbols for now as those aren't being codesign correctly right now.
        size_t bufferSize = 0;
        for (auto& sharedCacheRegion : sharedCacheRegions) {
            if ( (dyldSharedCache->header.localSymbolsSize != 0) && (sharedCacheRegion.first == dyldSharedCache->header.localSymbolsOffset) )
                continue;
            bufferSize += (sharedCacheRegion.second - sharedCacheRegion.first);
        }
        uint32_t slotCountToProcess = (uint32_t)((bufferSize + pageSize - 1) / pageSize);

        for (unsigned i = 0; i != slotCountToProcess; ++i) {
            // Skip data pages as those may have been slid by ASLR in the extracted file
            uint64_t fileOffset = i * csPageSize;
            bool isDataPage = false;
            for (unsigned mappingIndex = 0; mappingIndex != dyldSharedCache->header.mappingCount; ++mappingIndex) {
                if ( (mappings[mappingIndex].maxProt & VM_PROT_WRITE) == 0 )
                    continue;
                if ( (fileOffset >= mappings[mappingIndex].fileOffset) && (fileOffset < (mappings[mappingIndex].fileOffset + mappings[mappingIndex].size)) ) {
                    isDataPage = true;
                    break;
                }
            }
            if ( isDataPage )
                continue;

            CCDigest(dscDigestFormat, (uint8_t*)mapped_cache + fileOffset, (size_t)csPageSize, cdHashBuffer);
            uint8_t* cacheCdHashBuffer = hashSlot + (i * cd->hashSize);
            if (memcmp(cdHashBuffer, cacheCdHashBuffer, cd->hashSize) != 0)  {
                fprintf(stderr, "Error: dyld shared cache code signature for page %d is incorrect.\n", i);
                return -1;
            }
        }
    }
    return 0;
}

// A MappedCache provides access to all the parts of a cache file, even those typically not mapped at runtime
struct MappedCache {
    const DyldSharedCache*  dyldCache               = nullptr;
    size_t                  fileSize                = 0;
    size_t                  vmSize                  = 0;
};

// mmap() an shared cache file read/only but laid out like it would be at runtime
static std::optional<MappedCache> mapCacheFile(const char* path,
                                               uint64_t baseCacheUnslidAddress,
                                               uint8_t* buffer,
                                               bool isLocalSymbolsCache)
{
    struct stat statbuf;
    if ( ::stat(path, &statbuf) ) {
        fprintf(stderr, "Error: stat failed for dyld shared cache at %s\n", path);
        return {};
    }

    int cache_fd = ::open(path, O_RDONLY);
    if (cache_fd < 0) {
        fprintf(stderr, "Error: failed to open shared cache file at %s\n", path);
        return {};
    }

    uint8_t  firstPage[4096];
    if ( ::pread(cache_fd, firstPage, 4096, 0) != 4096 ) {
        fprintf(stderr, "Error: failed to read shared cache file at %s\n", path);
        return {};
    }
    const dyld_cache_header*       header   = (dyld_cache_header*)firstPage;
    if ( strstr(header->magic, "dyld_v1") != header->magic ) {
        fprintf(stderr, "Error: Invalid cache magic in file at %s\n", path);
        return {};
    }
    if ( header->mappingCount == 0 ) {
        fprintf(stderr, "Error: No mapping in shared cache file at %s\n", path);
        return {};
    }

    // Use the cache code signature to see if the cache file is valid.
    // Note we do this now, as we don't even want to trust the mappings are valid.
    {
        void* mapped_cache = ::mmap(nullptr, (size_t)statbuf.st_size, PROT_READ, MAP_PRIVATE, cache_fd, 0);
        if (mapped_cache == MAP_FAILED) {
            fprintf(stderr, "Error: mmap() for shared cache at %s failed, errno=%d\n", path, errno);
            return {};
        }
        int isValidResult = sharedCacheIsValid(mapped_cache, statbuf.st_size);
        ::munmap(mapped_cache, (size_t)statbuf.st_size);
        if ( isValidResult != 0 ) {
            fprintf(stderr, "Error: shared cache failed validity check for file at %s\n", path);
            return {};
        }
    }

    // The local symbols cache just wants an mmap as we don't want to change offsets there
    if ( isLocalSymbolsCache ) {
        void* mapped_cache = ::mmap(nullptr, (size_t)statbuf.st_size, PROT_READ, MAP_PRIVATE, cache_fd, 0);
        if (mapped_cache == MAP_FAILED) {
            fprintf(stderr, "Error: mmap() for shared cache at %s failed, errno=%d\n", path, errno);
            return {};
        }

        ::close(cache_fd);

        MappedCache mappedCache;
        mappedCache.dyldCache = (DyldSharedCache*)mapped_cache;
        mappedCache.fileSize  = (size_t)statbuf.st_size;

        return mappedCache;
    }

    const dyld_cache_mapping_info* mappings = (dyld_cache_mapping_info*)(firstPage + header->mappingOffset);
    const dyld_cache_mapping_info* lastMapping = &mappings[header->mappingCount - 1];

    // Allocate enough space for the cache and all subCaches
    uint64_t subCacheBufferOffset = 0;
    size_t vmSize = (size_t)((DyldSharedCache*)header)->mappedSize();
    if ( baseCacheUnslidAddress == 0 ) {
        // If the size is 0, then we might be looking directly at a sub cache.  In that case just allocate a buffer large
        // enough for its mappings.
        if ( vmSize == 0 ) {
            vmSize = (size_t)(lastMapping->address + lastMapping->size - mappings[0].address);
        }
        vm_address_t result;
        kern_return_t r = ::vm_allocate(mach_task_self(), &result, vmSize, VM_FLAGS_ANYWHERE);
        if ( r != KERN_SUCCESS ) {
            fprintf(stderr, "Error: failed to allocate space to load shared cache file at %s\n", path);
            return {};
        }
        buffer = (uint8_t*)result;
    } else {
        subCacheBufferOffset = mappings[0].address - baseCacheUnslidAddress;
    }

    for (uint32_t i=0; i < header->mappingCount; ++i) {
        uint64_t mappingAddressOffset = mappings[i].address - mappings[0].address;
        void* mapped_cache = ::mmap((void*)(buffer + mappingAddressOffset + subCacheBufferOffset), (size_t)mappings[i].size,
                                    PROT_READ, MAP_FIXED | MAP_PRIVATE, cache_fd, mappings[i].fileOffset);
        if (mapped_cache == MAP_FAILED) {
            fprintf(stderr, "Error: mmap() for shared cache at %s failed, errno=%d\n", path, errno);
            return {};
        }
    }

    ::close(cache_fd);

    MappedCache mappedCache;
    mappedCache.dyldCache = (DyldSharedCache*)(buffer + subCacheBufferOffset);
    mappedCache.fileSize = (size_t)statbuf.st_size;
    mappedCache.vmSize = vmSize;

    return mappedCache;
}

struct CacheFiles {
    std::vector<MappedCache> caches;

    // Keep track of the vm allocated space for the cache buffer so that we can free it later
    vm_address_t    cacheBuffer             = 0;
    vm_size_t       allocatedBufferSize     = 0;

    // Local symbols are in an mmap()ed region
    std::optional<MappedCache> localSymbolsCache;

    void unload() {
        vm_deallocate(mach_task_self(), cacheBuffer, allocatedBufferSize);
        if ( localSymbolsCache.has_value() )
            ::munmap((void*)localSymbolsCache->dyldCache, localSymbolsCache->fileSize);
    }
};

static CacheFiles mapCacheFiles(const char* path)
{
    std::optional<MappedCache> mappedCache = mapCacheFile(path, 0, nullptr, false);
    if ( !mappedCache.has_value() )
        return {};

    std::vector<MappedCache> caches;
    caches.push_back(mappedCache.value());

    const DyldSharedCache* cache = mappedCache.value().dyldCache;

    // Load all subcaches, if we have them
    if ( cache->header.mappingOffset >= __offsetof(dyld_cache_header, subCacheArrayCount) ) {
        if ( cache->header.subCacheArrayCount != 0 ) {
            const dyld_subcache_entry* subCacheEntries = (dyld_subcache_entry*)((uint8_t*)cache + cache->header.subCacheArrayOffset);

            for (uint32_t i = 0; i != cache->header.subCacheArrayCount; ++i) {
                std::string subCachePath = std::string(path) + "." + dyld3::json::decimal(i + 1);
                std::optional<MappedCache> mappedSubCache = mapCacheFile(subCachePath.c_str(), cache->unslidLoadAddress(), (uint8_t*)cache, false);
                if ( !mappedSubCache.has_value() )
                    return {};

                const DyldSharedCache* subCache = mappedSubCache.value().dyldCache;

                if ( memcmp(subCache->header.uuid, subCacheEntries[i].uuid, 16) != 0 ) {
                    uuid_string_t expectedUUIDString;
                    uuid_unparse_upper(subCacheEntries[i].uuid, expectedUUIDString);
                    uuid_string_t foundUUIDString;
                    uuid_unparse_upper(subCache->header.uuid, foundUUIDString);
                    fprintf(stderr, "Error: SubCache[%i] UUID mismatch.  Expected %s, got %s\n", i, expectedUUIDString, foundUUIDString);
                    return {};
                }

                caches.push_back(mappedSubCache.value());
            }
        }
    }

    // On old caches, the locals come from the same file we are extracting from
    std::string localSymbolsCachePath = path;
    if ( cache->hasLocalSymbolsInfoFile() ) {
        // On new caches, the locals come from a new subCache file
        if ( endsWith(localSymbolsCachePath, ".development") )
            localSymbolsCachePath.resize(localSymbolsCachePath.size() - strlen(".development"));
        localSymbolsCachePath += ".symbols";
    }

    std::optional<MappedCache> localSymbolsMappedCache = mapCacheFile(localSymbolsCachePath.c_str(), 0, nullptr, true);
    if ( localSymbolsMappedCache.has_value() && cache->hasLocalSymbolsInfoFile() ) {
        // Validate the UUID of the symbols file
        const DyldSharedCache* subCache = localSymbolsMappedCache.value().dyldCache;

        if ( memcmp(subCache->header.uuid, cache->header.symbolFileUUID, 16) != 0 ) {
            uuid_string_t expectedUUIDString;
            uuid_unparse_upper(cache->header.symbolFileUUID, expectedUUIDString);
            uuid_string_t foundUUIDString;
            uuid_unparse_upper(subCache->header.uuid, foundUUIDString);
            fprintf(stderr, "Error: Symbols subCache UUID mismatch.  Expected %s, got %s\n", expectedUUIDString, foundUUIDString);
            return {};
        }
    }

    CacheFiles cacheFiles;
    cacheFiles.caches = std::move(caches);
    cacheFiles.cacheBuffer = (vm_address_t)cacheFiles.caches.front().dyldCache;
    cacheFiles.allocatedBufferSize = (vm_size_t)cacheFiles.caches.front().vmSize;
    cacheFiles.localSymbolsCache = localSymbolsMappedCache;
    return cacheFiles;
}

struct seg_info
{
    seg_info(const char* n, uint64_t o, uint64_t s)
    : segName(n), offset(o), sizem(s) { }
    const char* segName;
    uint64_t    offset;
    uint64_t    sizem;
};

class CStringHash {
public:
    size_t operator()(const char* __s) const {
        size_t __h = 0;
        for ( ; *__s; ++__s)
            __h = 5 * __h + *__s;
        return __h;
    };
};
class CStringEquals {
public:
    bool operator()(const char* left, const char* right) const { return (strcmp(left, right) == 0); }
};
typedef std::unordered_map<const char*, std::vector<seg_info>, CStringHash, CStringEquals> NameToSegments;

// Filter to find individual symbol re-exports in trie
class NotReExportSymbol {
public:
    NotReExportSymbol(const std::set<int> &rd) :_reexportDeps(rd) {}
    bool operator()(const ExportInfoTrie::Entry &entry) const {
        return isSymbolReExport(entry);
    }
private:
    bool isSymbolReExport(const ExportInfoTrie::Entry &entry) const {
        if ( (entry.info.flags & EXPORT_SYMBOL_FLAGS_KIND_MASK) != EXPORT_SYMBOL_FLAGS_KIND_REGULAR )
            return true;
        if ( (entry.info.flags & EXPORT_SYMBOL_FLAGS_REEXPORT) == 0 )
            return true;
        // If the symbol comes from a dylib that is re-exported, this is not an individual symbol re-export
        if ( _reexportDeps.count((int)entry.info.other) != 0 )
            return true;
        return false;
    }
    const std::set<int> &_reexportDeps;
};

template <typename P>
struct LoadCommandInfo {
};

template <typename A>
class LinkeditOptimizer {
    typedef typename A::P P;
    typedef typename A::P::E E;
    typedef typename A::P::uint_t pint_t;

private:
    const uint8_t* linkeditBaseAddress = nullptr;
    macho_segment_command<P>* linkEditSegCmd = nullptr;
    symtab_command* symtab = nullptr;
    dysymtab_command*    dynamicSymTab = nullptr;
    linkedit_data_command*    functionStarts = nullptr;
    linkedit_data_command*    dataInCode = nullptr;
    uint32_t exportsTrieOffset = 0;
    uint32_t exportsTrieSize = 0;
    std::set<int> reexportDeps;
    
public:
    
    void optimize_loadcommands(dyld3::MachOAnalyzer* mh, const DyldSharedCache* dyldCache)
    {
        // update header flags
        mh->flags &= 0x7FFFFFFF; // remove in-cache bit
        
        // update load commands
        __block uint64_t cumulativeFileSize = 0;
        __block int depIndex = 0;
        Diagnostics diag;
        mh->forEachLoadCommand(diag, ^(const load_command* cmd, bool &stop) {
            switch ( cmd->cmd ) {
                case macho_segment_command<P>::CMD: {
                    auto segCmd = (macho_segment_command<P>*)cmd;
                    if ( strcmp(segCmd->segname(), "__LINKEDIT") == 0 ) {
                        linkEditSegCmd = segCmd;
                        linkeditBaseAddress = (uint8_t*)dyldCache + (segCmd->vmaddr() - dyldCache->unslidLoadAddress());
                        // The dataoff field in load commands is relative to the start of the file, so subtract the file offset
                        // here to account for that
                        linkeditBaseAddress -= segCmd->fileoff();
                    }
                    segCmd->set_fileoff(cumulativeFileSize);
                    segCmd->set_filesize(segCmd->vmsize());

                    auto const sectionsStart = (macho_section<P>*)((char*)segCmd + sizeof(macho_segment_command<P>));
                    auto const sectionsEnd = &sectionsStart[segCmd->nsects()];
                    for (auto sect = sectionsStart; sect < sectionsEnd; ++sect) {
                        if ( sect->offset() != 0 ) {
                            sect->set_offset((uint32_t)(cumulativeFileSize + sect->addr() - segCmd->vmaddr()));
                        }
                    }
                    cumulativeFileSize += segCmd->filesize();
                } break;
                case LC_DYLD_INFO_ONLY: {
                    // zero out all dyld info. lldb only uses symbol table
                    auto dyldInfo = (dyld_info_command*)cmd;
                    exportsTrieOffset =  dyldInfo->export_off;
                    exportsTrieSize = dyldInfo->export_size;
                    dyldInfo->rebase_off = 0;
                    dyldInfo->rebase_size = 0;
                    dyldInfo->bind_off = 0;
                    dyldInfo->bind_size = 0;
                    dyldInfo->weak_bind_off = 0;
                    dyldInfo->weak_bind_size = 0;
                    dyldInfo->lazy_bind_off = 0;
                    dyldInfo->lazy_bind_size = 0;
                    dyldInfo->export_off = 0;
                    dyldInfo->export_size = 0;
                } break;
                case LC_DYLD_EXPORTS_TRIE: {
                    // don't put export trie into extracted dylib.  lldb only uses symbol table
                    linkedit_data_command* exportsTrie = (linkedit_data_command*)cmd;
                    exportsTrieOffset =  exportsTrie->dataoff;
                    exportsTrieSize   = exportsTrie->datasize;
                    exportsTrie->dataoff = 0;
                    exportsTrie->datasize = 0;
                } break;
                case LC_SYMTAB:
                    symtab = (symtab_command*)cmd;
                    break;
                case LC_DYSYMTAB:
                    dynamicSymTab = (dysymtab_command*)cmd;
                    break;
                case LC_FUNCTION_STARTS:
                    functionStarts = (linkedit_data_command*)cmd;
                    break;
                case LC_DATA_IN_CODE:
                    dataInCode = (linkedit_data_command*)cmd;
                    break;
                case LC_LOAD_DYLIB:
                case LC_LOAD_WEAK_DYLIB:
                case LC_REEXPORT_DYLIB:
                case LC_LOAD_UPWARD_DYLIB:
                    depIndex++;
                    if ( cmd->cmd == LC_REEXPORT_DYLIB ) {
                        reexportDeps.insert(depIndex);
                    }
                    break;
                default:
                    break;
            }
        });

        mh->removeLoadCommand(diag, ^(const load_command* cmd, bool& remove, bool &stop) {
            switch ( cmd->cmd ) {
                case LC_SEGMENT_SPLIT_INFO:
                    // <rdar://problem/23212513> dylibs iOS 9 dyld caches have bogus LC_SEGMENT_SPLIT_INFO
                    remove = true;
                    stop = true;
                    break;
                default:
                    break;
            }
        });
}

    int optimize_linkedit(std::vector<uint8_t> &new_linkedit_data, uint64_t textOffsetInCache,
                          std::optional<const DyldSharedCache*> localSymbolsCache)
    {
        // rebuild symbol table
        if ( (linkEditSegCmd == nullptr) || (linkeditBaseAddress == nullptr) ) {
            fprintf(stderr, "__LINKEDIT not found\n");
            return -1;
        }
        if ( symtab == nullptr ) {
            fprintf(stderr, "LC_SYMTAB not found\n");
            return -1;
        }
        if ( dynamicSymTab == nullptr ) {
            fprintf(stderr, "LC_DYSYMTAB not found\n");
            return -1;
        }

        const uint64_t newFunctionStartsOffset = new_linkedit_data.size();
        uint32_t functionStartsSize = 0;
        if ( functionStarts != NULL ) {
            // copy function starts from original cache file to new mapped dylib file
            functionStartsSize = functionStarts->datasize;
            new_linkedit_data.insert(new_linkedit_data.end(),
                                     linkeditBaseAddress + functionStarts->dataoff,
                                     linkeditBaseAddress + functionStarts->dataoff + functionStartsSize);
        }

        // pointer align
        while ((linkEditSegCmd->fileoff() + new_linkedit_data.size()) % sizeof(pint_t))
            new_linkedit_data.push_back(0);

        const uint64_t newDataInCodeOffset = new_linkedit_data.size();
        uint32_t dataInCodeSize = 0;
        if ( dataInCode != NULL ) {
            // copy data-in-code info from original cache file to new mapped dylib file
            dataInCodeSize = dataInCode->datasize;
            new_linkedit_data.insert(new_linkedit_data.end(),
                                     linkeditBaseAddress + dataInCode->dataoff,
                                     linkeditBaseAddress + dataInCode->dataoff + dataInCodeSize);
        }

        std::vector<ExportInfoTrie::Entry> exports;
        if ( exportsTrieSize != 0 ) {
            const uint8_t* exportsStart = linkeditBaseAddress + exportsTrieOffset;
            const uint8_t* exportsEnd = &exportsStart[exportsTrieSize];
            ExportInfoTrie::parseTrie(exportsStart, exportsEnd, exports);
            exports.erase(std::remove_if(exports.begin(), exports.end(), NotReExportSymbol(reexportDeps)), exports.end());
        }

        __block macho_nlist<P>* localNlists = nullptr;
        __block uint32_t        localNlistCount = 0;
        if ( localSymbolsCache.has_value() ) {
            const DyldSharedCache* localsCache = *localSymbolsCache;

            macho_nlist<P>*         allLocalNlists = (macho_nlist<P>*)localsCache->getLocalNlistEntries();
            localsCache->forEachLocalSymbolEntry(^(uint64_t dylibCacheVMOffset, uint32_t nlistStartIndex, uint32_t nlistCount, bool& stop){
                if (dylibCacheVMOffset == textOffsetInCache) {
                    localNlists     = &allLocalNlists[nlistStartIndex];
                    localNlistCount = nlistCount;
                    stop            = true;
                }
            });
        }

        // compute number of symbols in new symbol table
        const macho_nlist<P>*       mergedSymTabStart = (macho_nlist<P>*)(linkeditBaseAddress + symtab->symoff);
        const macho_nlist<P>* const mergedSymTabend   = &mergedSymTabStart[symtab->nsyms];
        uint32_t newSymCount = symtab->nsyms;
        if ( localNlistCount != 0 ) {
            // if we are recombining with unmapped locals, recompute new total size
            newSymCount = localNlistCount + dynamicSymTab->nextdefsym + dynamicSymTab->nundefsym;
        }

        // add room for N_INDR symbols for re-exported symbols
        newSymCount += exports.size();

        // copy symbol entries and strings from original cache file to new mapped dylib file
        const char* mergedStringPoolStart = (const char*)linkeditBaseAddress + symtab->stroff;
        const char* mergedStringPoolEnd = &mergedStringPoolStart[symtab->strsize];

        // First count how many entries we need
        std::vector<macho_nlist<P>> newSymTab;
        newSymTab.reserve(newSymCount);
        std::vector<char> newSymNames;

        // first pool entry is always empty string
        newSymNames.push_back('\0');

        // local symbols are first in dylibs, if this cache has unmapped locals, insert them all first
        uint32_t undefSymbolShift = 0;
        if ( localNlistCount != 0 ) {
            const DyldSharedCache* localsCache = *localSymbolsCache;
            const char* localStrings = localsCache->getLocalStrings();
            undefSymbolShift = localNlistCount - dynamicSymTab->nlocalsym;
            // update load command to reflect new count of locals
            dynamicSymTab->ilocalsym = (uint32_t)newSymTab.size();
            dynamicSymTab->nlocalsym = localNlistCount;
            // copy local symbols
            for (uint32_t i=0; i < localNlistCount; ++i) {
                const char* localName = &localStrings[localNlists[i].n_strx()];
                if ( localName > localStrings + localsCache->getLocalStringsSize() )
                    localName = "<corrupt local symbol name>";
                macho_nlist<P> t = localNlists[i];
                t.set_n_strx((uint32_t)newSymNames.size());
                newSymNames.insert(newSymNames.end(),
                                   localName,
                                   localName + (strlen(localName) + 1));
                newSymTab.push_back(t);
            }
            // now start copying symbol table from start of externs instead of start of locals
            mergedSymTabStart = &mergedSymTabStart[dynamicSymTab->iextdefsym];
        }
        // copy full symbol table from cache (skipping locals if they where elsewhere)
        for (const macho_nlist<P>* s = mergedSymTabStart; s != mergedSymTabend; ++s) {
            macho_nlist<P> t = *s;
            t.set_n_strx((uint32_t)newSymNames.size());
            const char* symName = &mergedStringPoolStart[s->n_strx()];
            if ( symName > mergedStringPoolEnd )
                symName = "<corrupt symbol name>";
            newSymNames.insert(newSymNames.end(),
                               symName,
                               symName + (strlen(symName) + 1));
            newSymTab.push_back(t);
        }
        // <rdar://problem/16529213> recreate N_INDR symbols in extracted dylibs for debugger
        for (std::vector<ExportInfoTrie::Entry>::iterator it = exports.begin(); it != exports.end(); ++it) {
            macho_nlist<P> t;
            memset(&t, 0, sizeof(t));
            t.set_n_strx((uint32_t)newSymNames.size());
            t.set_n_type(N_INDR | N_EXT);
            t.set_n_sect(0);
            t.set_n_desc(0);
            newSymNames.insert(newSymNames.end(),
                               it->name.c_str(),
                               it->name.c_str() + (it->name.size() + 1));
            const char* importName = it->info.importName.c_str();
            if ( *importName == '\0' )
                importName = it->name.c_str();
            t.set_n_value(newSymNames.size());
            newSymNames.insert(newSymNames.end(),
                               importName,
                               importName + (strlen(importName) + 1));
            newSymTab.push_back(t);
        }

        if ( newSymCount != newSymTab.size() ) {
            fprintf(stderr, "symbol count miscalculation\n");
            return -1;
        }

        //const uint64_t newStringPoolOffset = newIndSymTabOffset + dynamicSymTab->nindirectsyms()*sizeof(uint32_t);
        //macho_nlist<P>* const newSymTabStart = (macho_nlist<P>*)(((uint8_t*)mh) + newSymTabOffset);
        //char* const newStringPoolStart = (char*)mh + newStringPoolOffset;

        // pointer align
        while ((linkEditSegCmd->fileoff() + new_linkedit_data.size()) % sizeof(pint_t))
            new_linkedit_data.push_back(0);

        const uint64_t newSymTabOffset = new_linkedit_data.size();

        // Copy sym tab
        for (macho_nlist<P>& sym : newSymTab) {
            uint8_t symData[sizeof(macho_nlist<P>)];
            memcpy(&symData, &sym, sizeof(sym));
            new_linkedit_data.insert(new_linkedit_data.end(), &symData[0], &symData[sizeof(macho_nlist<P>)]);
        }

        const size_t newIndSymTabOffset = new_linkedit_data.size();

        // Copy (and adjust) indirect symbol table
        const uint32_t* mergedIndSymTab = (uint32_t*)(linkeditBaseAddress + dynamicSymTab->indirectsymoff);
        new_linkedit_data.insert(new_linkedit_data.end(),
                                 (char*)mergedIndSymTab,
                                 (char*)(mergedIndSymTab + dynamicSymTab->nindirectsyms));
        if ( undefSymbolShift != 0 ) {
            uint32_t* newIndSymTab = (uint32_t*)&new_linkedit_data[newIndSymTabOffset];
            for (uint32_t i=0; i < dynamicSymTab->nindirectsyms; ++i) {
                newIndSymTab[i] += undefSymbolShift;
            }
        }
        const uint64_t newStringPoolOffset = new_linkedit_data.size();

        // pointer align string pool size
        while (newSymNames.size() % sizeof(pint_t))
            newSymNames.push_back('\0');

        new_linkedit_data.insert(new_linkedit_data.end(), newSymNames.begin(), newSymNames.end());

        // update load commands
        if ( functionStarts != NULL ) {
            functionStarts->dataoff = (uint32_t)(newFunctionStartsOffset + linkEditSegCmd->fileoff());
            functionStarts->datasize = functionStartsSize;
        }
        if ( dataInCode != NULL ) {
            dataInCode->dataoff = (uint32_t)(newDataInCodeOffset + linkEditSegCmd->fileoff());
            dataInCode->datasize = dataInCodeSize;
        }

        symtab->nsyms = newSymCount;
        symtab->symoff = (uint32_t)(newSymTabOffset + linkEditSegCmd->fileoff());
        symtab->stroff = (uint32_t)(newStringPoolOffset + linkEditSegCmd->fileoff());
        symtab->strsize = (uint32_t)newSymNames.size();
        dynamicSymTab->extreloff = 0;
        dynamicSymTab->nextrel = 0;
        dynamicSymTab->locreloff = 0;
        dynamicSymTab->nlocrel = 0;
        dynamicSymTab->indirectsymoff = (uint32_t)(newIndSymTabOffset + linkEditSegCmd->fileoff());
        linkEditSegCmd->set_filesize(symtab->stroff + symtab->strsize - linkEditSegCmd->fileoff());
        linkEditSegCmd->set_vmsize((linkEditSegCmd->filesize() + 4095) & (-4096));

        return 0;
    }

};

static void make_dirs(const char* file_path)
{
    //printf("make_dirs(%s)\n", file_path);
    char dirs[strlen(file_path)+1];
    strcpy(dirs, file_path);
    char* lastSlash = strrchr(dirs, '/');
    if ( lastSlash == NULL )
        return;
    lastSlash[1] = '\0';
    struct stat stat_buf;
    if ( stat(dirs, &stat_buf) != 0 ) {
        char* afterSlash = &dirs[1];
        char* slash;
        while ( (slash = strchr(afterSlash, '/')) != NULL ) {
            *slash = '\0';
            ::mkdir(dirs, S_IRWXU | S_IRGRP|S_IXGRP | S_IROTH|S_IXOTH);
            //printf("mkdir(%s)\n", dirs);
            *slash = '/';
            afterSlash = slash+1;
        }
    }
}



template <typename A>
void dylib_maker(const void* mapped_cache, std::optional<const DyldSharedCache*> localSymbolsCache,
                 std::vector<uint8_t> &dylib_data, const std::vector<seg_info>& segments) {

    size_t  additionalSize  = 0;
    for(std::vector<seg_info>::const_iterator it=segments.begin(); it != segments.end(); ++it) {
        if ( strcmp(it->segName, "__LINKEDIT") != 0 )
            additionalSize += it->sizem;
    }

    std::vector<uint8_t> new_dylib_data;
    new_dylib_data.reserve(additionalSize);

    // Write regular segments into the buffer
    uint64_t                textOffsetInCache    = 0;
    for( std::vector<seg_info>::const_iterator it=segments.begin(); it != segments.end(); ++it) {

        if(strcmp(it->segName, "__TEXT") == 0 )
            textOffsetInCache = it->offset;

        //printf("segName=%s, offset=0x%llX, size=0x%0llX\n", it->segName, it->offset, it->sizem);
        // Copy all but the __LINKEDIT.  It will be copied later during the optimizer in to a temporary buffer but it would
        // not be efficient to copy it all now for each dylib.
        if (strcmp(it->segName, "__LINKEDIT") == 0 )
            continue;
        std::copy(((uint8_t*)mapped_cache)+it->offset, ((uint8_t*)mapped_cache)+it->offset+it->sizem, std::back_inserter(new_dylib_data));
    }

    // optimize linkedit
    std::vector<uint8_t> new_linkedit_data;
    new_linkedit_data.reserve(1 << 20);

    LinkeditOptimizer<A> linkeditOptimizer;
    dyld3::MachOAnalyzer* mh = (dyld3::MachOAnalyzer*)&new_dylib_data.front();
    linkeditOptimizer.optimize_loadcommands(mh, ((DyldSharedCache*)mapped_cache));
    linkeditOptimizer.optimize_linkedit(new_linkedit_data, textOffsetInCache, localSymbolsCache);

    new_dylib_data.insert(new_dylib_data.end(), new_linkedit_data.begin(), new_linkedit_data.end());

    // Page align file
    while (new_dylib_data.size() % 4096)
        new_dylib_data.push_back(0);

    dylib_data.insert(dylib_data.end(), new_dylib_data.begin(), new_dylib_data.end());
}

typedef __typeof(dylib_maker<x86>) dylib_maker_func;
typedef void (^progress_block)(unsigned current, unsigned total);

struct SharedCacheExtractor;
struct SharedCacheDylibExtractor {
    SharedCacheDylibExtractor(const char* name, std::vector<seg_info> segInfo)
        : name(name), segInfo(segInfo) { }

    void extractCache(SharedCacheExtractor& context);

    const char*                     name;
    const std::vector<seg_info>     segInfo;
    int                             result = 0;
};

struct SharedCacheExtractor {
    SharedCacheExtractor(const NameToSegments& map,
                         const char* extraction_root_path,
                         dylib_maker_func* dylib_create_func,
                         const void* mapped_cache,
                         std::optional<const DyldSharedCache*> localSymbolsCache,
                         progress_block progress)
        : map(map), extraction_root_path(extraction_root_path),
          dylib_create_func(dylib_create_func), mapped_cache(mapped_cache),
          localSymbolsCache(localSymbolsCache),
          progress(progress) {

      extractors.reserve(map.size());
      for (auto it : map)
          extractors.emplace_back(it.first, it.second);

        // Limit the number of open files.  16 seems to give better performance than higher numbers.
        sema = dispatch_semaphore_create(16);
    }
    int extractCaches();

    static void extractCache(void *ctx, size_t i);

    const NameToSegments&                   map;
    std::vector<SharedCacheDylibExtractor>  extractors;
    dispatch_semaphore_t                    sema;
    const char*                             extraction_root_path;
    dylib_maker_func*                       dylib_create_func;
    const void*                             mapped_cache;
    std::optional<const DyldSharedCache*>   localSymbolsCache;
    progress_block                          progress;
    std::atomic_int                         count = { 0 };
};

int SharedCacheExtractor::extractCaches() {
    dispatch_queue_t process_queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW, 0);
    dispatch_apply_f(map.size(), process_queue,
                     this, extractCache);

    int result = 0;
    for (const SharedCacheDylibExtractor& extractor : extractors) {
        if (extractor.result != 0) {
            result = extractor.result;
            break;
        }
    }
    return result;
}

void SharedCacheExtractor::extractCache(void *ctx, size_t i) {
    SharedCacheExtractor& context = *(SharedCacheExtractor*)ctx;
    dispatch_semaphore_wait(context.sema, DISPATCH_TIME_FOREVER);
    context.extractors[i].extractCache(context);
    dispatch_semaphore_signal(context.sema);
}

void SharedCacheDylibExtractor::extractCache(SharedCacheExtractor &context) {

    char    dylib_path[PATH_MAX];
    strcpy(dylib_path, context.extraction_root_path);
    strcat(dylib_path, "/");
    strcat(dylib_path, name);

    //printf("%s with %lu segments\n", dylib_path, it->second.size());
    // make sure all directories in this path exist
    make_dirs(dylib_path);

    // open file, create if does not already exist
    int fd = ::open(dylib_path, O_CREAT | O_TRUNC | O_EXLOCK | O_RDWR, 0644);
    if ( fd == -1 ) {
        fprintf(stderr, "can't open or create dylib file %s, errnor=%d\n", dylib_path, errno);
        result = -1;
        return;
    }

    std::vector<uint8_t> vec;
    context.dylib_create_func(context.mapped_cache, context.localSymbolsCache, vec, segInfo);
    context.progress(context.count++, (unsigned)context.map.size());

    // Write file data
    if( write(fd, &vec.front(), vec.size()) == -1) {
        fprintf(stderr, "error writing, errnor=%d\n", errno);
        result = -1;
    }

    close(fd);
}

int dyld_shared_cache_extract_dylibs_progress(const char* shared_cache_file_path, const char* extraction_root_path,
                                              progress_block progress)
{
    CacheFiles mappedCaches = mapCacheFiles(shared_cache_file_path);
    if ( mappedCaches.caches.empty() )
        return -1;

    const DyldSharedCache* mapped_cache = mappedCaches.caches.front().dyldCache;

    // instantiate arch specific dylib maker
    dylib_maker_func* dylib_create_func = nullptr;
    if ( strcmp((char*)mapped_cache, "dyld_v1    i386") == 0 )
        dylib_create_func = dylib_maker<x86>;
    else if ( strcmp((char*)mapped_cache, "dyld_v1  x86_64") == 0 )
        dylib_create_func = dylib_maker<x86_64>;
    else if ( strcmp((char*)mapped_cache, "dyld_v1 x86_64h") == 0 )
        dylib_create_func = dylib_maker<x86_64>;
    else if ( strcmp((char*)mapped_cache, "dyld_v1   armv5") == 0 )
        dylib_create_func = dylib_maker<arm>;
    else if ( strcmp((char*)mapped_cache, "dyld_v1   armv6") == 0 )
        dylib_create_func = dylib_maker<arm>;
    else if ( strcmp((char*)mapped_cache, "dyld_v1   armv7") == 0 )
        dylib_create_func = dylib_maker<arm>;
    else if ( strncmp((char*)mapped_cache, "dyld_v1  armv7", 14) == 0 )
        dylib_create_func = dylib_maker<arm>;
    else if ( strcmp((char*)mapped_cache, "dyld_v1   arm64") == 0 )
        dylib_create_func = dylib_maker<arm64>;
#if SUPPORT_ARCH_arm64e
    else if ( strcmp((char*)mapped_cache, "dyld_v1  arm64e") == 0 )
        dylib_create_func = dylib_maker<arm64>;
#endif
#if SUPPORT_ARCH_arm64_32
    else if ( strcmp((char*)mapped_cache, "dyld_v1arm64_32") == 0 )
        dylib_create_func = dylib_maker<arm64_32>;
#endif
    else {
        fprintf(stderr, "Error: unrecognized dyld shared cache magic.\n");
        mappedCaches.unload();
        return -1;
    }

    // iterate through all images in cache and build map of dylibs and segments
    __block NameToSegments  map;
    int                     result = 0;

    if ( mapped_cache->mappedSize() == 0 ) {
        mappedCaches.unload();
        return result;
    }

    mapped_cache->forEachImage(^(const mach_header *mh, const char *installName) {
        ((const dyld3::MachOAnalyzer*)mh)->forEachSegment(^(const dyld3::MachOAnalyzer::SegmentInfo &info, bool &stop) {
            map[installName].push_back(seg_info(info.segName, info.vmAddr - mapped_cache->unslidLoadAddress(), info.vmSize));
        });
    });

    if( result != 0 ) {
        fprintf(stderr, "Error: dyld_shared_cache_iterate_segments_with_slide failed.\n");
        mappedCaches.unload();
        return result;
    }

    // for each dylib instantiate a dylib file
    std::optional<const DyldSharedCache*> localSymbolsCache;
    if ( mappedCaches.localSymbolsCache.has_value() )
        localSymbolsCache = mappedCaches.localSymbolsCache->dyldCache;
    SharedCacheExtractor extractor(map, extraction_root_path, dylib_create_func,
                                   mapped_cache, localSymbolsCache, progress);
    result = extractor.extractCaches();

    mappedCaches.unload();
    return result;
}



int dyld_shared_cache_extract_dylibs(const char* shared_cache_file_path, const char* extraction_root_path)
{
    return dyld_shared_cache_extract_dylibs_progress(shared_cache_file_path, extraction_root_path,
                                                     ^(unsigned , unsigned) {} );
}


#if 0
// test program
#include <stdio.h>
#include <stddef.h>
#include <dlfcn.h>


typedef int (*extractor_proc)(const char* shared_cache_file_path, const char* extraction_root_path,
                              void (^progress)(unsigned current, unsigned total));

int main(int argc, const char* argv[])
{
    if ( argc != 3 ) {
        fprintf(stderr, "usage: dsc_extractor <path-to-cache-file> <path-to-device-dir>\n");
        return 1;
    }

    //void* handle = dlopen("/Volumes/my/src/dyld/build/Debug/dsc_extractor.bundle", RTLD_LAZY);
    void* handle = dlopen("/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/usr/lib/dsc_extractor.bundle", RTLD_LAZY);
    if ( handle == NULL ) {
        fprintf(stderr, "dsc_extractor.bundle could not be loaded\n");
        return 1;
    }

    extractor_proc proc = (extractor_proc)dlsym(handle, "dyld_shared_cache_extract_dylibs_progress");
    if ( proc == NULL ) {
        fprintf(stderr, "dsc_extractor.bundle did not have dyld_shared_cache_extract_dylibs_progress symbol\n");
        return 1;
    }

    int result = (*proc)(argv[1], argv[2], ^(unsigned c, unsigned total) { printf("%d/%d\n", c, total); } );
    fprintf(stderr, "dyld_shared_cache_extract_dylibs_progress() => %d\n", result);
    return 0;
}


#endif




