/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2009-2012 Apple Inc. All rights reserved.
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

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syslimits.h>
#include <mach-o/arch.h>
#include <mach-o/nlist.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/dyld_priv.h>
#include <bootstrap.h>
#include <mach/mach.h>
#include <dispatch/dispatch.h>
#include <uuid/uuid.h>

#include <TargetConditionals.h>

#include <map>
#include <vector>
#include <iostream>
#include <optional>

//#include "ClosureBuilder.h"
#include "DyldSharedCache.h"
#include "JSONWriter.h"
#include "Trie.hpp"
#include "dsc_extractor.h"
#include "dyld_introspection.h"
#include "OptimizerSwift.h"

#include "PrebuiltLoader.h"
#include "DyldProcessConfig.h"
#include "DyldRuntimeState.h"

#include "objc-shared-cache.h"
#include "OptimizerObjC.h"

using namespace dyld4;

#if TARGET_OS_OSX
#define DSC_BUNDLE_REL_PATH "../../lib/dsc_extractor.bundle"
#else
#define DSC_BUNDLE_REL_PATH "../lib/dsc_extractor.bundle"
#endif

// In newer shared caches, relative method list selectors are offsets from the magic selector in libobjc
static uint64_t getSharedCacheRelativeSelectorBaseVMAddress(const DyldSharedCache* dyldCache, bool rebased)
{
    __block uint64_t sharedCacheRelativeSelectorBaseVMAddress = 0;
    if ( dyldCache->header.mappingOffset >= __offsetof(dyld_cache_header, symbolFileUUID) ) {
        constexpr std::string_view magicSelector = "\xf0\x9f\xa4\xaf";
        dyld3::MachOAnalyzer::VMAddrConverter vmAddrConverter = dyldCache->makeVMAddrConverter(rebased);
        dyldCache->forEachImage(^(const mach_header *mh, const char *installName) {
            if ( !strcmp(installName, "/usr/lib/libobjc.A.dylib") ) {
                const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)mh;
                uintptr_t slide = ma->getSlide();
                Diagnostics diag;
                ma->forEachObjCSelectorReference(diag, vmAddrConverter,
                                                 ^(uint64_t selRefVMAddr, uint64_t selRefTargetVMAddr, bool &stop) {
                    const char* selString = (const char*)selRefTargetVMAddr + slide;
                    if ( selString == magicSelector ) {
                        sharedCacheRelativeSelectorBaseVMAddress = selRefTargetVMAddr;
                    }
                });
            }
        });
    }
    return sharedCacheRelativeSelectorBaseVMAddress;
}

enum Mode {
    modeNone,
    modeList,
    modeMap,
    modeDependencies,
    modeSlideInfo,
    modeVerboseSlideInfo,
    modeTextInfo,
    modeLinkEdit,
    modeLocalSymbols,
    modeJSONMap,
    modeJSONDependents,
    modeSectionSizes,
    modeStrings,
    modeInfo,
    modeSize,
    modeObjCInfo,
    modeObjCProtocols,
    modeObjCImpCaches,
    modeObjCClasses,
    modeObjCSelectors,
    modeSwiftProtocolConformances,
    modeExtract,
    modePatchTable,
    modeListDylibsWithSection
};

struct Options {
    Mode            mode;
    const char*     dependentsOfPath;
    const char*     extractionDir;
    const char*     segmentName;
    const char*     sectionName;
    const char*     rootPath            = nullptr;
    bool            printUUIDs;
    bool            printVMAddrs;
    bool            printDylibVersions;
    bool            printInodes;
};


static void usage() {
    fprintf(stderr, "Usage: dyld_shared_cache_util -list [ -uuid ] [-vmaddr] | -dependents <dylib-path> [ -versions ] | -linkedit | -map | -slide_info | -verbose_slide_info | -info | -extract <dylib-dir>  [ shared-cache-file ] \n");
}

static void checkMode(Mode mode) {
    if ( mode != modeNone ) {
        fprintf(stderr, "Error: select one of: -list, -dependents, -info, -slide_info, -verbose_slide_info, -linkedit, -map, -extract, or -size\n");
        usage();
        exit(1);
    }
}

struct SegmentInfo
{
    uint64_t    vmAddr;
    uint64_t    vmSize;
    const char* installName;
    const char* segName;
};

static void buildSegmentInfo(const DyldSharedCache* dyldCache, std::vector<SegmentInfo>& segInfos)
{
    dyldCache->forEachImage(^(const mach_header* mh, const char* installName) {
        dyld3::MachOAnalyzer* ma = (dyld3::MachOAnalyzer*)mh;
        ma->forEachSegment(^(const dyld3::MachOAnalyzer::SegmentInfo& info, bool& stop) {
            // Note, we subtract 1 from the vmSize so that lower_bound doesn't include the end of the segment
            // as being a match for a given address.
            segInfos.push_back({info.vmAddr, info.vmSize - 1, installName, info.segName});
        });
    });

    std::sort(segInfos.begin(), segInfos.end(), [](const SegmentInfo& l, const SegmentInfo& r) -> bool {
        return l.vmAddr < r.vmAddr;
    });
}

static void printSlideInfoForDataRegion(const DyldSharedCache* dyldCache, uint64_t dataStartAddress, uint64_t dataSize,
                                        const uint8_t* dataPagesStart,
                                        const dyld_cache_slide_info* slideInfoHeader, bool verboseSlideInfo) {

    printf("slide info version=%d\n", slideInfoHeader->version);
    if ( slideInfoHeader->version == 1 ) {
        printf("toc_count=%d, data page count=%lld\n", slideInfoHeader->toc_count, dataSize/4096);
        const dyld_cache_slide_info_entry* entries = (dyld_cache_slide_info_entry*)((char*)slideInfoHeader + slideInfoHeader->entries_offset);
        const uint16_t* tocs = (uint16_t*)((char*)slideInfoHeader + slideInfoHeader->toc_offset);
        for(int i=0; i < slideInfoHeader->toc_count; ++i) {
            printf("0x%08llX: [% 5d,% 5d] ", dataStartAddress + i*4096, i, tocs[i]);
            const dyld_cache_slide_info_entry* entry = &entries[tocs[i]];
            for(int j=0; j < slideInfoHeader->entries_size; ++j)
                printf("%02X", entry->bits[j]);
            printf("\n");
        }
    }
    else if ( slideInfoHeader->version == 2 ) {
        const dyld_cache_slide_info2* slideInfo = (dyld_cache_slide_info2*)(slideInfoHeader);
        printf("page_size=%d\n", slideInfo->page_size);
        printf("delta_mask=0x%016llX\n", slideInfo->delta_mask);
        printf("value_add=0x%016llX\n", slideInfo->value_add);
        printf("page_starts_count=%d, page_extras_count=%d\n", slideInfo->page_starts_count, slideInfo->page_extras_count);
        const uint16_t* starts = (uint16_t* )((char*)slideInfo + slideInfo->page_starts_offset);
        const uint16_t* extras = (uint16_t* )((char*)slideInfo + slideInfo->page_extras_offset);
        for (int i=0; i < slideInfo->page_starts_count; ++i) {
            const uint16_t start = starts[i];
            auto rebaseChain = [&](uint8_t* pageContent, uint16_t startOffset)
            {
                uintptr_t slideAmount = 0;
                const uintptr_t   deltaMask    = (uintptr_t)(slideInfo->delta_mask);
                const uintptr_t   valueMask    = ~deltaMask;
                const uintptr_t   valueAdd     = (uintptr_t)(slideInfo->value_add);
                const unsigned    deltaShift   = __builtin_ctzll(deltaMask) - 2;

                uint32_t pageOffset = startOffset;
                uint32_t delta = 1;
                while ( delta != 0 ) {
                    uint8_t* loc = pageContent + pageOffset;
                    uintptr_t rawValue = *((uintptr_t*)loc);
                    delta = (uint32_t)((rawValue & deltaMask) >> deltaShift);
                    uintptr_t value = (rawValue & valueMask);
                    if ( value != 0 ) {
                        value += valueAdd;
                        value += slideAmount;
                    }
                    printf("    [% 5d + 0x%04llX]: 0x%016llX = 0x%016llX\n", i, (uint64_t)(pageOffset), (uint64_t)rawValue, (uint64_t)value);
                    pageOffset += delta;
                }
            };
            if ( start == DYLD_CACHE_SLIDE_PAGE_ATTR_NO_REBASE ) {
                printf("page[% 5d]: no rebasing\n", i);
            }
            else if ( start & DYLD_CACHE_SLIDE_PAGE_ATTR_EXTRA ) {
                printf("page[% 5d]: ", i);
                int j=(start & 0x3FFF);
                bool done = false;
                do {
                    uint16_t aStart = extras[j];
                    printf("start=0x%04X ", aStart & 0x3FFF);
                    if ( verboseSlideInfo ) {
                        uint8_t* page = (uint8_t*)(long)(dataPagesStart + (slideInfo->page_size*i));
                        uint16_t pageStartOffset = (aStart & 0x3FFF)*4;
                        rebaseChain(page, pageStartOffset);
                    }
                    done = (extras[j] & DYLD_CACHE_SLIDE_PAGE_ATTR_END);
                    ++j;
                } while ( !done );
                printf("\n");
            }
            else {
                printf("page[% 5d]: start=0x%04X\n", i, starts[i]);
                if ( verboseSlideInfo ) {
                    uint8_t* page = (uint8_t*)(long)(dataPagesStart + (slideInfo->page_size*i));
                    uint16_t pageStartOffset = start*4;
                    rebaseChain(page, pageStartOffset);
                }
            }
        }
    }
    else if ( slideInfoHeader->version == 3 ) {
        const dyld_cache_slide_info3* slideInfo = (dyld_cache_slide_info3*)(slideInfoHeader);
        printf("page_size=%d\n", slideInfo->page_size);
        printf("page_starts_count=%d\n", slideInfo->page_starts_count);
        printf("auth_value_add=0x%016llX\n", slideInfo->auth_value_add);
        const uintptr_t authValueAdd = (uintptr_t)(slideInfo->auth_value_add);
        for (int i=0; i < slideInfo->page_starts_count; ++i) {
            uint16_t delta = slideInfo->page_starts[i];
            if ( delta == DYLD_CACHE_SLIDE_V3_PAGE_ATTR_NO_REBASE ) {
                printf("page[% 5d]: no rebasing\n", i);
                continue;
            }

            printf("page[% 5d]: start=0x%04X\n", i, delta);
            if ( !verboseSlideInfo )
                continue;

            delta = delta/sizeof(uint64_t); // initial offset is byte based
            const uint8_t* pageStart = dataPagesStart + (i * slideInfo->page_size);
            const dyld_cache_slide_pointer3* loc = (dyld_cache_slide_pointer3*)pageStart;
            do {
                loc += delta;
                delta = loc->plain.offsetToNextPointer;
                dyld3::MachOLoaded::ChainedFixupPointerOnDisk ptr;
                ptr.raw64 = *((uint64_t*)loc);
                if ( loc->auth.authenticated ) {
                    uint64_t target = authValueAdd + loc->auth.offsetFromSharedCacheBase;
                    uint64_t targetValue = target;
#if __has_feature(ptrauth_calls)
                    targetValue = ptr.arm64e.signPointer((void*)loc, target);
#endif
                    printf("    [% 5d + 0x%04llX]: 0x%016llX (JOP: diversity %d, address %s, %s)\n",
                           i, (uint64_t)((const uint8_t*)loc - pageStart), targetValue,
                           ptr.arm64e.authBind.diversity, ptr.arm64e.authBind.addrDiv ? "true" : "false",
                           ptr.arm64e.keyName());
                }
                else {
                    uint64_t targetValue = ptr.arm64e.unpackTarget();
                    printf("    [% 5d + 0x%04llX]: 0x%016llX\n", i, (uint64_t)((const uint8_t*)loc - pageStart), targetValue);
                }
            } while (delta != 0);
        }
    }
    else if ( slideInfoHeader->version == 4 ) {
        const dyld_cache_slide_info4* slideInfo = (dyld_cache_slide_info4*)(slideInfoHeader);
        printf("page_size=%d\n", slideInfo->page_size);
        printf("delta_mask=0x%016llX\n", slideInfo->delta_mask);
        printf("value_add=0x%016llX\n", slideInfo->value_add);
        printf("page_starts_count=%d, page_extras_count=%d\n", slideInfo->page_starts_count, slideInfo->page_extras_count);
        const uint16_t* starts = (uint16_t* )((char*)slideInfo + slideInfo->page_starts_offset);
        const uint16_t* extras = (uint16_t* )((char*)slideInfo + slideInfo->page_extras_offset);
        for (int i=0; i < slideInfo->page_starts_count; ++i) {
            const uint16_t start = starts[i];
            auto rebaseChainV4 = [&](uint8_t* pageContent, uint16_t startOffset)
            {
                uintptr_t slideAmount = 0;
                const uintptr_t   deltaMask    = (uintptr_t)(slideInfo->delta_mask);
                const uintptr_t   valueMask    = ~deltaMask;
                const uintptr_t   valueAdd     = (uintptr_t)(slideInfo->value_add);
                const unsigned    deltaShift   = __builtin_ctzll(deltaMask) - 2;

                uint32_t pageOffset = startOffset;
                uint32_t delta = 1;
                while ( delta != 0 ) {
                    uint8_t* loc = pageContent + pageOffset;
                    uint32_t rawValue = *((uint32_t*)loc);
                    delta = (uint32_t)((rawValue & deltaMask) >> deltaShift);
                    uintptr_t value = (rawValue & valueMask);
                    if ( (value & 0xFFFF8000) == 0 ) {
                        // small positive non-pointer, use as-is
                    }
                    else if ( (value & 0x3FFF8000) == 0x3FFF8000 ) {
                        // small negative non-pointer
                        value |= 0xC0000000;
                    }
                    else  {
                        value += valueAdd;
                        value += slideAmount;
                    }
                    printf("    [% 5d + 0x%04X]: 0x%08X\n", i, pageOffset, rawValue);
                    pageOffset += delta;
                }
            };
            if ( start == DYLD_CACHE_SLIDE4_PAGE_NO_REBASE ) {
                printf("page[% 5d]: no rebasing\n", i);
            }
            else if ( start & DYLD_CACHE_SLIDE4_PAGE_USE_EXTRA ) {
                printf("page[% 5d]: ", i);
                int j=(start & DYLD_CACHE_SLIDE4_PAGE_INDEX);
                bool done = false;
                do {
                    uint16_t aStart = extras[j];
                    printf("start=0x%04X ", aStart & DYLD_CACHE_SLIDE4_PAGE_INDEX);
                    if ( verboseSlideInfo ) {
                        uint8_t* page = (uint8_t*)(long)(dataPagesStart + (slideInfo->page_size*i));
                        uint16_t pageStartOffset = (aStart & DYLD_CACHE_SLIDE4_PAGE_INDEX)*4;
                        rebaseChainV4(page, pageStartOffset);
                    }
                    done = (extras[j] & DYLD_CACHE_SLIDE4_PAGE_EXTRA_END);
                    ++j;
                } while ( !done );
                printf("\n");
            }
            else {
                printf("page[% 5d]: start=0x%04X\n", i, starts[i]);
                if ( verboseSlideInfo ) {
                    uint8_t* page = (uint8_t*)(long)(dataPagesStart + (slideInfo->page_size*i));
                    uint16_t pageStartOffset = start*4;
                    rebaseChainV4(page, pageStartOffset);
                }
            }
        }
    }
}


static void findImageAndSegment(const DyldSharedCache* dyldCache, const std::vector<SegmentInfo>& segInfos, uint64_t cacheOffset, SegmentInfo* found)
{
    const uint64_t locVmAddr = dyldCache->unslidLoadAddress() + cacheOffset;
    const SegmentInfo target = { locVmAddr, 0, NULL, NULL };
    const auto lowIt = std::lower_bound(segInfos.begin(), segInfos.end(), target,
                                                                        [](const SegmentInfo& l, const SegmentInfo& r) -> bool {
                                                                            return l.vmAddr+l.vmSize < r.vmAddr+r.vmSize;
                                                                    });
    *found = *lowIt;
}


int main (int argc, const char* argv[]) {

    const char* sharedCachePath = nullptr;

    Options options;
    options.mode = modeNone;
    options.printUUIDs = false;
    options.printVMAddrs = false;
    options.printDylibVersions = false;
    options.printInodes = false;
    options.dependentsOfPath = NULL;
    options.extractionDir = NULL;

    bool printStrings = false;
    bool printExports = false;

    for (uint32_t i = 1; i < argc; i++) {
        const char* opt = argv[i];
        if (opt[0] == '-') {
            if (strcmp(opt, "-list") == 0) {
                checkMode(options.mode);
                options.mode = modeList;
            }
            else if (strcmp(opt, "-dependents") == 0) {
                checkMode(options.mode);
                options.mode = modeDependencies;
                options.dependentsOfPath = argv[++i];
                if ( i >= argc ) {
                    fprintf(stderr, "Error: option -depdendents requires an argument\n");
                    usage();
                    exit(1);
                }
            }
            else if (strcmp(opt, "-linkedit") == 0) {
                checkMode(options.mode);
                options.mode = modeLinkEdit;
            }
            else if (strcmp(opt, "-info") == 0) {
                checkMode(options.mode);
                options.mode = modeInfo;
            }
            else if (strcmp(opt, "-slide_info") == 0) {
                checkMode(options.mode);
                options.mode = modeSlideInfo;
            }
            else if (strcmp(opt, "-verbose_slide_info") == 0) {
                checkMode(options.mode);
                options.mode = modeVerboseSlideInfo;
            }
            else if (strcmp(opt, "-text_info") == 0) {
                checkMode(options.mode);
                options.mode = modeTextInfo;
            }
            else if (strcmp(opt, "-local_symbols") == 0) {
                checkMode(options.mode);
                options.mode = modeLocalSymbols;
            }
            else if (strcmp(opt, "-strings") == 0) {
                if (options.mode != modeStrings)
                    checkMode(options.mode);
                options.mode = modeStrings;
                printStrings = true;
            }
            else if (strcmp(opt, "-sections") == 0) {
                checkMode(options.mode);
                options.mode = modeSectionSizes;
            }
            else if (strcmp(opt, "-exports") == 0) {
                if (options.mode != modeStrings)
                    checkMode(options.mode);
                options.mode = modeStrings;
                printExports = true;
            }
            else if (strcmp(opt, "-map") == 0) {
                checkMode(options.mode);
                options.mode = modeMap;
            }
            else if (strcmp(opt, "-json-map") == 0) {
                checkMode(options.mode);
                options.mode = modeJSONMap;
            }
            else if (strcmp(opt, "-json-dependents") == 0) {
                checkMode(options.mode);
                options.mode = modeJSONDependents;
            }
            else if (strcmp(opt, "-size") == 0) {
                checkMode(options.mode);
                options.mode = modeSize;
            }
            else if (strcmp(opt, "-objc-info") == 0) {
                checkMode(options.mode);
                options.mode = modeObjCInfo;
            }
            else if (strcmp(opt, "-objc-protocols") == 0) {
                checkMode(options.mode);
                options.mode = modeObjCProtocols;
            }
            else if (strcmp(opt, "-objc-imp-caches") == 0) {
                checkMode(options.mode);
                options.mode = modeObjCImpCaches;
            }
            else if (strcmp(opt, "-objc-classes") == 0) {
                checkMode(options.mode);
                options.mode = modeObjCClasses;
            }
            else if (strcmp(opt, "-objc-selectors") == 0) {
                checkMode(options.mode);
                options.mode = modeObjCSelectors;
            }
            else if (strcmp(opt, "-fs-root") == 0) {
                options.rootPath = argv[++i];
            }
            else if (strcmp(opt, "-swift-proto") == 0) {
                checkMode(options.mode);
                options.mode = modeSwiftProtocolConformances;
            }
            else if (strcmp(opt, "-extract") == 0) {
                checkMode(options.mode);
                options.mode = modeExtract;
                options.extractionDir = argv[++i];
                if ( i >= argc ) {
                    fprintf(stderr, "Error: option -extract requires a directory argument\n");
                    usage();
                    exit(1);
                }
            }
            else if (strcmp(opt, "-uuid") == 0) {
                options.printUUIDs = true;
            }
            else if (strcmp(opt, "-inode") == 0) {
                options.printInodes = true;
            }
            else if (strcmp(opt, "-versions") == 0) {
                options.printDylibVersions = true;
            }
            else if (strcmp(opt, "-vmaddr") == 0) {
                options.printVMAddrs = true;
            }
            else if (strcmp(opt, "-patch_table") == 0) {
                options.mode = modePatchTable;
            }
            else if (strcmp(opt, "-list_dylibs_with_section") == 0) {
                options.mode = modeListDylibsWithSection;
                options.segmentName = argv[++i];
                options.sectionName = argv[++i];
                if ( i >= argc ) {
                    fprintf(stderr, "Error: option -list_dylibs_with_section requires a segment and section name\n");
                    usage();
                    exit(1);
                }
            }
            else {
                fprintf(stderr, "Error: unrecognized option %s\n", opt);
                usage();
                exit(1);
            }
        }
        else {
            sharedCachePath = opt;
        }
    }

    if ( options.mode == modeNone ) {
        fprintf(stderr, "Error: select one of -list, -dependents, -info, -linkedit, or -map\n");
        usage();
        exit(1);
    }

    if ( options.mode != modeSlideInfo && options.mode != modeVerboseSlideInfo ) {
        if ( options.printUUIDs && (options.mode != modeList) )
            fprintf(stderr, "Warning: -uuid option ignored outside of -list mode\n");

        if ( options.printVMAddrs && (options.mode != modeList) )
            fprintf(stderr, "Warning: -vmaddr option ignored outside of -list mode\n");

        if ( options.printDylibVersions && (options.mode != modeDependencies) )
            fprintf(stderr, "Warning: -versions option ignored outside of -dependents mode\n");

        if ( (options.mode == modeDependencies) && (options.dependentsOfPath == NULL) ) {
            fprintf(stderr, "Error: -dependents given, but no dylib path specified\n");
            usage();
            exit(1);
        }
    }

    __block std::vector<const DyldSharedCache*> dyldCaches;

    const DyldSharedCache* dyldCache = nullptr;
    if ( sharedCachePath != nullptr ) {
        dyldCaches = DyldSharedCache::mapCacheFiles(sharedCachePath);
        // mapCacheFile prints an error if something goes wrong, so just return in that case.
        if ( dyldCaches.empty() )
            return 1;
        dyldCache = dyldCaches.front();
    }
    else {
        size_t cacheLength;
        dyldCache = (DyldSharedCache*)_dyld_get_shared_cache_range(&cacheLength);
        if (dyldCache == nullptr) {
            fprintf(stderr, "Could not get in-memory shared cache\n");
            return 1;
        }
        if ( options.mode == modeObjCClasses ) {
            fprintf(stderr, "Cannot use -objc-classes with a live cache.  Please run with a path to an on-disk cache file\n");
            return 1;
        }


        // The in-use cache might be the first cache file of many.  In that case, also add the sub caches
        dyldCache->forEachCache(^(const DyldSharedCache *cache, bool& stopCache) {
            dyldCaches.push_back(dyldCache);
        });
    }

    if ( options.mode == modeSlideInfo || options.mode == modeVerboseSlideInfo ) {
        if ( !dyldCache->hasSlideInfo() ) {
            fprintf(stderr, "Error: dyld shared cache does not contain slide info\n");
            exit(1);
        }

        const bool verboseSlideInfo = (options.mode == modeVerboseSlideInfo);
        dyldCache->forEachSlideInfo(^(uint64_t mappingStartAddress, uint64_t mappingSize, const uint8_t *mappingPagesStart,
                                      uint64_t slideInfoOffset, uint64_t slideInfoSize, const dyld_cache_slide_info *slideInfoHeader) {
            printSlideInfoForDataRegion(dyldCache, mappingStartAddress, mappingSize, mappingPagesStart,
                                        slideInfoHeader, verboseSlideInfo);
        });
        return 0;
    }
    else if ( options.mode == modeInfo ) {
        const dyld_cache_header* header = &dyldCache->header;
        uuid_string_t uuidString;
        uuid_unparse_upper(header->uuid, uuidString);
        printf("uuid: %s\n", uuidString);

        dyld3::Platform platform = dyldCache->platform();
        printf("platform: %s\n", dyld3::MachOFile::platformName(platform));
        printf("built by: %s\n", header->locallyBuiltCache ? "local machine" : "B&I");
        printf("cache type: %s\n", header->cacheType ? "production" : "development");
        if ( header->mappingOffset >= __offsetof(dyld_cache_header, imagesCount) ) {
            printf("image count: %u\n", header->imagesCount);
        } else {
            printf("image count: %u\n", header->imagesCountOld);
        }
        if ( (header->mappingOffset >= 0x78) && (header->branchPoolsOffset != 0) ) {
            printf("branch pool count:  %u\n", header->branchPoolsCount);
        }
        {
            uint32_t pageSize            = 0x4000; // fix me for intel
            uint32_t possibleSlideValues = (uint32_t)(header->maxSlide/pageSize);
            uint32_t entropyBits = 0;
            if ( possibleSlideValues > 1 )
                entropyBits = __builtin_clz(possibleSlideValues - 1);
            printf("ASLR entropy: %u-bits (%lldMB)\n", entropyBits, header->maxSlide >> 20);
        }

        printf("mappings:\n");
        dyldCache->forEachRange(^(const char *mappingName, uint64_t unslidVMAddr, uint64_t vmSize,
                                  uint32_t cacheFileIndex, uint64_t fileOffset, uint32_t initProt, uint32_t maxProt, bool& stopRange) {
            printf("%16s %4lluMB,  file offset: #%u/0x%08llX -> 0x%08llX,  address: 0x%08llX -> 0x%08llX\n",
                   mappingName, vmSize / (1024*1024), cacheFileIndex, fileOffset, fileOffset + vmSize, unslidVMAddr, unslidVMAddr + vmSize);
        }, ^(const DyldSharedCache* subCache, uint32_t cacheFileIndex) {
            const dyld_cache_header* subCacheHeader = &subCache->header;

            if ( subCacheHeader->codeSignatureOffset != 0 ) {
                uint64_t size = subCacheHeader->codeSignatureSize;
                uint64_t csAddr = subCache->getCodeSignAddress();
                if ( size != 0 )
                    printf("%16s %4lluMB,  file offset: #%u/0x%08llX -> 0x%08llX,  address: 0x%08llX -> 0x%08llX\n",
                           "code sign", size/(1024*1024), cacheFileIndex,
                           subCacheHeader->codeSignatureOffset, subCacheHeader->codeSignatureOffset + size, csAddr, csAddr + size);
            }

            if ( subCacheHeader->mappingOffset > __offsetof(dyld_cache_header, rosettaReadOnlySize) ) {
                if ( subCacheHeader->rosettaReadOnlySize != 0 ) {
                    printf("Rosetta RO:      %4lluMB,                                          address: 0x%08llX -> 0x%08llX\n",
                           subCacheHeader->rosettaReadOnlySize/(1024*1024), subCacheHeader->rosettaReadOnlyAddr,
                           subCacheHeader->rosettaReadOnlyAddr + subCacheHeader->rosettaReadOnlySize);
                }
                if ( subCacheHeader->rosettaReadWriteSize != 0 ) {
                    printf("Rosetta RW:      %4lluMB,                                          address: 0x%08llX -> 0x%08llX\n",
                           subCacheHeader->rosettaReadWriteSize/(1024*1024), subCacheHeader->rosettaReadWriteAddr,
                           subCacheHeader->rosettaReadWriteAddr + subCacheHeader->rosettaReadWriteSize);
                }
            }

            subCache->forEachSlideInfo(^(uint64_t mappingStartAddress, uint64_t mappingSize, const uint8_t *mappingPagesStart,
                                         uint64_t slideInfoOffset, uint64_t slideInfoSize, const dyld_cache_slide_info *slideInfoHeader) {

                printf("slide info:      %4lluKB,  file offset: #%u/0x%08llX -> 0x%08llX\n",
                       slideInfoSize/1024, cacheFileIndex, slideInfoOffset, slideInfoOffset + slideInfoSize);
            });
            if ( subCacheHeader->localSymbolsOffset != 0 )
                printf("local symbols:    %3lluMB,  file offset: #%u/0x%08llX -> 0x%08llX\n",
                       subCacheHeader->localSymbolsSize/(1024*1024), cacheFileIndex,
                       subCacheHeader->localSymbolsOffset, subCacheHeader->localSymbolsOffset + subCacheHeader->localSymbolsSize);
        });
    }
    else if ( options.mode == modeTextInfo ) {
        const dyld_cache_header* header = &dyldCache->header;
        printf("dylib text infos (count=%llu):\n", header->imagesTextCount);
        dyldCache->forEachImageTextSegment(^(uint64_t loadAddressUnslid, uint64_t textSegmentSize, const unsigned char *dylibUUID, const char *installName, bool &stop) {
            uuid_string_t uuidString;
            uuid_unparse_upper(dylibUUID, uuidString);
            printf("   0x%09llX -> 0x%09llX  <%s>  %s\n", loadAddressUnslid, loadAddressUnslid + textSegmentSize, uuidString, installName);
        });
    }
    else if ( options.mode == modeLocalSymbols ) {
        if ( !dyldCache->hasLocalSymbolsInfo() && !dyldCache->hasLocalSymbolsInfoFile() ) {
            fprintf(stderr, "Error: dyld shared cache does not contain local symbols info\n");
            exit(1);
        }

        if ( sharedCachePath == nullptr ) {
            fprintf(stderr, "Cannot use -local_symbols with a live cache.  Please run with a path to an on-disk cache file\n");
            exit(1);
        }

        // The locals are in an unmapped part of the cache.  So use the introspection APIs to map them in
        // For now only support the case where the cache was passed in as a file, not the live cache
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
        const bool is64 = (strstr(dyldCache->archName(), "64") != nullptr) && (strstr(dyldCache->archName(), "64_32") == nullptr);
        bool mappedCacheFile = dyld_shared_cache_for_file(sharedCachePath, ^(dyld_shared_cache_t cache) {
            __block uint32_t entriesCount = 0;
            dyld_shared_cache_for_each_image(cache, ^(dyld_image_t image) {
                // FIXME: Use dyld_image_get_file_path(image) when its available
                const char* imageName = dyldCache->getIndexedImagePath(entriesCount);
                bool foundNList = dyld_image_local_nlist_content_4Symbolication(image,
                                                                                ^(const void* nlistStart, uint64_t nlistCount,
                                                                                  const char* stringTable) {
                    printf("Local symbols nlist for: %s\n", imageName);
                    if ( is64 ) {
                        const nlist_64* symTab = (nlist_64*)nlistStart;
                        for (int e = 0; e < nlistCount; ++e) {
                            const nlist_64* entry = &symTab[e];
                            printf("     nlist[%d].str=%d, %s\n", e, entry->n_un.n_strx, &stringTable[entry->n_un.n_strx]);
                            printf("     nlist[%d].value=0x%0llX\n", e, entry->n_value);
                        }
                    } else {
                        const struct nlist* symTab = (struct nlist*)nlistStart;
                        for (int e = 0; e < nlistCount; ++e) {
                            const struct nlist* entry = &symTab[e];
                            printf("     nlist[%d].str=%d, %s\n", e, entry->n_un.n_strx, &stringTable[entry->n_un.n_strx]);
                            printf("     nlist[%d].value=0x%0X\n", e, entry->n_value);
                        }
                    }
                });
                if ( !foundNList ) {
                    fprintf(stderr, "Error: Failed to find local symbols nlist for: %s\n", imageName);
                    exit(1);
                }
                entriesCount++;
            });
            printf("local symbols by dylib (count=%d):\n", entriesCount);
        });

        if ( !mappedCacheFile ) {
            fprintf(stderr, "Error: Failed to map local symbols for shared cache file\n");
            exit(1);
        }
#pragma clang diagnostic pop

#if 0
        const bool is64 = (strstr(dyldCache->archName(), "64") != NULL);
        const uint32_t nlistFileOffset = (uint32_t)((uint8_t*)dyldCache->getLocalNlistEntries() - (uint8_t*)dyldCache);
        const uint32_t nlistCount = dyldCache->getLocalNlistCount();
        const uint32_t nlistByteSize = is64 ? nlistCount*16 : nlistCount*12;
        const char* localStrings = dyldCache->getLocalStrings();
        const uint32_t stringsFileOffset = (uint32_t)((uint8_t*)localStrings - (uint8_t*)dyldCache);
        const uint32_t stringsSize = dyldCache->getLocalStringsSize();

        printf("local symbols nlist array:  %3uMB,  file offset: 0x%08X -> 0x%08X\n", nlistByteSize/(1024*1024), nlistFileOffset, nlistFileOffset+nlistByteSize);
        printf("local symbols string pool:  %3uMB,  file offset: 0x%08X -> 0x%08X\n", stringsSize/(1024*1024), stringsFileOffset, stringsFileOffset+stringsSize);

        __block uint32_t entriesCount = 0;
        dyldCache->forEachLocalSymbolEntry(^(uint64_t dylibCacheVMOffset, uint32_t nlistStartIndex, uint32_t nCount, bool &stop) {
            const char* imageName = dyldCache->getIndexedImagePath(entriesCount);
            printf("   nlistStartIndex=%5d, nlistCount=%5d, image=%s\n", nlistStartIndex, nCount, imageName);
#if 0
            if ( is64 ) {
                const nlist_64* symTab = (nlist_64*)((char*)dyldCache + nlistFileOffset);
                for (int e = 0; e < nlistLocalCount; ++e) {
                    const nlist_64* entry = &symTab[nlistStartIndex + e];
                    printf("     nlist[%d].str=%d, %s\n", e, entry->n_un.n_strx, &localStrings[entry->n_un.n_strx]);
                    printf("     nlist[%d].value=0x%0llX\n", e, entry->n_value);
                }
            }
#endif
            entriesCount++;
        });
        printf("local symbols by dylib (count=%d):\n", entriesCount);
#endif
    }
    else if ( options.mode == modeJSONMap ) {
        std::string buffer = dyldCache->generateJSONMap("unknown");
        printf("%s\n", buffer.c_str());
    }
    else if ( options.mode == modeJSONDependents ) {
        std::cout <<  dyldCache->generateJSONDependents();
    }
    else if ( options.mode == modeStrings ) {
        if (printStrings) {
            // The cache has not been slid if we loaded it from disk
            bool cacheRebased = (sharedCachePath == nullptr);
            dyld3::MachOAnalyzer::VMAddrConverter vmAddrConverter = dyldCache->makeVMAddrConverter(cacheRebased);
            if ( !cacheRebased )
                dyldCache->applyCacheRebases();

            uint64_t sharedCacheRelativeSelectorBaseVMAddress = getSharedCacheRelativeSelectorBaseVMAddress(dyldCache, cacheRebased);

            dyldCache->forEachImage(^(const mach_header *mh, const char *installName) {
                __block std::unordered_set<std::string_view> seenStrings;
                const dyld3::MachOAnalyzer* ma = (dyld3::MachOAnalyzer*)mh;
                int64_t slide = ma->getSlide();
                uint32_t pointerSize = ma->pointerSize();

                ma->forEachSection(^(const dyld3::MachOAnalyzer::SectionInfo& info, bool malformedSectionRange, bool& stop) {
                    if ( ( (info.sectFlags & SECTION_TYPE) == S_CSTRING_LITERALS ) ) {
                        if ( malformedSectionRange ) {
                            stop = true;
                            return;
                        }
                        const uint8_t* content = (uint8_t*)(info.sectAddr + slide);
                        const char* s   = (char*)content;
                        const char* end = s + info.sectSize;
                        while ( s < end ) {
                            printf("%s: %s\n", installName, s);
                            seenStrings.insert(s);
                            while (*s != '\0' )
                                ++s;
                            ++s;
                        }
                    }
                });

                // objc string sections are coalesced in the builder, so might not be present above
                // Find referenced objc strings by walking the other objc metadata
                auto printString = ^(uint64_t stringVMAddr) {
                    const char* selString = (const char*)stringVMAddr + slide;
                    auto itAndInserted = seenStrings.insert(selString);
                    if ( itAndInserted.second )
                        printf("%s: %s\n", installName, selString);
                };

                auto visitMethod = ^(uint64_t methodVMAddr, const dyld3::MachOAnalyzer::ObjCMethod& method,
                                     bool& stopMethod) {
                    printString(method.nameVMAddr);
                    printString(method.typesVMAddr);
                };

                auto visitProperty = ^(uint64_t propertyVMAddr, const dyld3::MachOAnalyzer::ObjCProperty& property) {
                    printString(property.nameVMAddr);
                };

                Diagnostics diag;
                ma->forEachObjCSelectorReference(diag, vmAddrConverter,
                                                 ^(uint64_t selRefVMAddr, uint64_t selRefTargetVMAddr, bool &stop) {
                    printString(selRefTargetVMAddr);
                });

                // If the cache hasn't been rebased, then we can also print other objc metadata, such as classes
                // If we are doing this, then we need to patch the cache to undo the bit-stealing in the ASLR format
                if ( !cacheRebased ) {
                    auto visitClass = ^(uint64_t classVMAddr,
                                        uint64_t classSuperclassVMAddr, uint64_t classDataVMAddr,
                                        const dyld3::MachOAnalyzer::ObjCClassInfo& objcClass, bool isMetaClass,
                                        bool& stop) {
                        printString(objcClass.nameVMAddr(pointerSize));
                        ma->forEachObjCMethod(objcClass.baseMethodsVMAddr(pointerSize), vmAddrConverter, sharedCacheRelativeSelectorBaseVMAddress, visitMethod);
                        ma->forEachObjCProperty(objcClass.basePropertiesVMAddr(pointerSize), vmAddrConverter, visitProperty);
                    };

                    auto visitCategory = ^(uint64_t categoryVMAddr,
                                           const dyld3::MachOAnalyzer::ObjCCategory& objcCategory,
                                           bool& stopCategory) {
                        printString(objcCategory.nameVMAddr);
                        ma->forEachObjCMethod(objcCategory.instanceMethodsVMAddr, vmAddrConverter, sharedCacheRelativeSelectorBaseVMAddress, visitMethod);
                        ma->forEachObjCMethod(objcCategory.classMethodsVMAddr, vmAddrConverter, sharedCacheRelativeSelectorBaseVMAddress, visitMethod);
                        ma->forEachObjCMethod(objcCategory.instanceMethodsVMAddr, vmAddrConverter, sharedCacheRelativeSelectorBaseVMAddress, visitMethod);
                        ma->forEachObjCProperty(objcCategory.instancePropertiesVMAddr, vmAddrConverter, visitProperty);
                    };

                    auto visitProtocol = ^(uint64_t protoVMAddr,
                                           const dyld3::MachOAnalyzer::ObjCProtocol& objcProto,
                                           bool& stopProtocol) {
                        printString(objcProto.nameVMAddr);
                        ma->forEachObjCMethod(objcProto.instanceMethodsVMAddr, vmAddrConverter, sharedCacheRelativeSelectorBaseVMAddress, visitMethod);
                        ma->forEachObjCMethod(objcProto.classMethodsVMAddr, vmAddrConverter, sharedCacheRelativeSelectorBaseVMAddress, visitMethod);
                        ma->forEachObjCMethod(objcProto.optionalInstanceMethodsVMAddr, vmAddrConverter, sharedCacheRelativeSelectorBaseVMAddress, visitMethod);
                        ma->forEachObjCMethod(objcProto.optionalClassMethodsVMAddr, vmAddrConverter, sharedCacheRelativeSelectorBaseVMAddress, visitMethod);
                    };

                    ma->forEachObjCClass(diag, vmAddrConverter, visitClass);
                    ma->forEachObjCCategory(diag, vmAddrConverter, visitCategory);
                    ma->forEachObjCProtocol(diag, vmAddrConverter, visitProtocol);
                }
            });
        }

        if (printExports) {
            dyldCache->forEachImage(^(const mach_header *mh, const char *installName) {
                const dyld3::MachOAnalyzer* ma = (dyld3::MachOAnalyzer*)mh;
                uint32_t exportTrieRuntimeOffset;
                uint32_t exportTrieSize;
                if ( ma->hasExportTrie(exportTrieRuntimeOffset, exportTrieSize) ) {
                    const uint8_t* start = (uint8_t*)mh + exportTrieRuntimeOffset;
                    const uint8_t* end = start + exportTrieSize;
                    std::vector<ExportInfoTrie::Entry> exports;
                    if ( !ExportInfoTrie::parseTrie(start, end, exports) ) {
                        return;
                    }

                    for (const ExportInfoTrie::Entry& entry: exports) {
                        printf("%s: %s\n", installName, entry.name.c_str());
                    }
                }
            });
        }
    }
    else if ( options.mode == modeSectionSizes ) {
        __block std::map<std::string, uint64_t> sectionSizes;
        dyldCache->forEachImage(^(const mach_header *mh, const char *installName) {
            const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)mh;
            ma->forEachSection(^(const dyld3::MachOAnalyzer::SectionInfo &sectInfo, bool malformedSectionRange, bool &stop) {
                std::string section = std::string(sectInfo.segInfo.segName) + " " + sectInfo.sectName;
                sectionSizes[section] += sectInfo.sectSize;
            });
        });
        for (const auto& keyAndValue : sectionSizes) {
            printf("%lld %s\n", keyAndValue.second, keyAndValue.first.c_str());
        }
    }
    else if ( options.mode == modeObjCInfo ) {
        const objc_opt::objc_opt_t* objcOpt = dyldCache->objcOpt();
        if ( objcOpt == nullptr ) {
            fprintf(stderr, "Error: could not get optimized objc\n");
            return 1;
        }

        printf("version:                            %u\n", objcOpt->version);
        printf("flags:                              0x%08x\n", objcOpt->flags);
        if ( const objc::SelectorHashTable* selectors = dyldCache->objcOpt()->selectorOpt() ) {
            printf("num selectors:                      %u\n", selectors->occupancy());
        }
        if ( const objc::ClassHashTable* classes = dyldCache->objcOpt()->classOpt() ) {
            printf("num classes:                        %u\n", classes->occupancy());
        }
        if ( const objc::ProtocolHashTable* protocols = dyldCache->objcOpt()->protocolOpt() ) {
            printf("num protocols:                      %u\n", protocols->occupancy());
        }
        if ( const void* relativeMethodListSelectorBase = dyldCache->objcOpt()->relativeMethodListsBaseAddress() ) {
            printf("method list selector base address:  0x%llx\n", dyldCache->unslidLoadAddress() + ((uint64_t)relativeMethodListSelectorBase - (uint64_t)dyldCache));
            printf("method list selector base value:    \"%s\"\n", (const char*)relativeMethodListSelectorBase);
        }
    }
    else if ( options.mode == modeObjCProtocols ) {
        if ( dyldCache->objcOpt() == nullptr ) {
            fprintf(stderr, "Error: could not get optimized objc\n");
            return 1;
        }
        const objc::ProtocolHashTable* protocols = dyldCache->objcOpt()->protocolOpt();
        if ( protocols == nullptr ) {
            fprintf(stderr, "Error: could not get optimized objc protocols\n");
            return 1;
        }

        protocols->forEachProtocol(^(uint32_t bucketIndex, const char* protocolName, const dyld3::Array<uint64_t>& implCacheOffsets) {

            if ( implCacheOffsets.empty() ) {
                // Empty bucket
                fprintf(stderr, "[% 5d]\n", bucketIndex);
                return;
            }

            if ( implCacheOffsets.count() == 1 ) {
                // No duplicates
                fprintf(stderr, "[% 5d] -> (% 8lld) = %s\n", bucketIndex, implCacheOffsets[0], protocolName);
                return;
            }

            // class appears in more than one header
            fprintf(stderr, "[% 5d] -> %lu duplicates = %s\n", bucketIndex, implCacheOffsets.count(), protocolName);
            for (uint64_t cacheOffset : implCacheOffsets) {
                fprintf(stderr, "  - [% 5d] -> (% 8lld) = %s\n", bucketIndex, cacheOffset, protocolName);
            }
        });
    }
    else if ( options.mode == modeObjCClasses ) {

        // If we are running on macOS against a cache for another device, then we need a root path to find on-disk dylibs/executables
        if ( (dyld3::Platform)dyld_get_active_platform() != dyldCache->platform() ) {
            if ( options.rootPath == nullptr ) {
                fprintf(stderr, "Analyzing cache file requires a root path for on-disk binaries.  Rerun with -fs-root *path*\n");
                return 1;
            }
        }

        dyldCache->applyCacheRebases();

        auto getString = ^const char *(const dyld3::MachOAnalyzer* ma, uint64_t nameVMAddr){
            dyld3::MachOAnalyzer::PrintableStringResult result;
            const char* name = ma->getPrintableString(nameVMAddr, result);
            if (result == dyld3::MachOAnalyzer::PrintableStringResult::CanPrint)
                return name;
            return nullptr;
        };

        // We don't actually slide the cache.  It still contains unslid VMAddr's
        const bool rebased = false;

        uint64_t sharedCacheRelativeSelectorBaseVMAddress = getSharedCacheRelativeSelectorBaseVMAddress(dyldCache, rebased);

        using dyld3::json::Node;
        using dyld3::json::NodeValueType;

        std::string instancePrefix("-");
        std::string classPrefix("+");

        // Build a map of class vm addrs to their names so that categories know the
        // name of the class they are attaching to
        __block std::unordered_map<uint64_t, const char*> classVMAddrToName;
        __block std::unordered_map<uint64_t, const char*> metaclassVMAddrToName;
        dyldCache->forEachImage(^(const mach_header *mh, const char *installName) {
            const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)mh;
            const uint32_t pointerSize = ma->pointerSize();

            auto visitClass = ^(uint64_t classVMAddr,
                                uint64_t classSuperclassVMAddr, uint64_t classDataVMAddr,
                                const dyld3::MachOAnalyzer::ObjCClassInfo& objcClass, bool isMetaClass,
                                bool& stop) {
                if (auto className = getString(ma, objcClass.nameVMAddr(pointerSize))) {
                    if (isMetaClass)
                        metaclassVMAddrToName[classVMAddr] = className;
                    else
                        classVMAddrToName[classVMAddr] = className;
                }
            };

            Diagnostics diag;

            dyld3::MachOAnalyzer::VMAddrConverter vmAddrConverter = dyldCache->makeVMAddrConverter(rebased);
            ma->forEachObjCClass(diag, vmAddrConverter, visitClass);
        });

        // These are used only for the on-disk binaries we analyze
        __block std::vector<const char*>        onDiskChainedFixupBindTargets;
        __block std::unordered_map<uint64_t, const char*> onDiskClassVMAddrToName;
        __block std::unordered_map<uint64_t, const char*> onDiskMetaclassVMAddrToName;

        auto getProperties = ^(const dyld3::MachOAnalyzer* ma, uint64_t propertiesVMAddr,
                               const dyld3::MachOAnalyzer::VMAddrConverter& vmAddrConverter) {
            __block Node propertiesNode;
            auto visitProperty = ^(uint64_t propertyVMAddr, const dyld3::MachOAnalyzer::ObjCProperty& property) {
                // Get the name && attributes
                auto propertyName = getString(ma, property.nameVMAddr);
                auto propertyAttributes = getString(ma, property.attributesVMAddr);

                if (!propertyName || !propertyAttributes)
                    return;

                Node propertyNode;
                propertyNode.map["name"] = Node{propertyName};
                propertyNode.map["attributes"] = Node{propertyAttributes};
                propertiesNode.array.push_back(propertyNode);
            };
            ma->forEachObjCProperty(propertiesVMAddr, vmAddrConverter, visitProperty);
            return propertiesNode.array.empty() ? std::optional<Node>() : propertiesNode;
        };

        auto getClassProtocols = ^(const dyld3::MachOAnalyzer* ma, uint64_t protocolsVMAddr,
                                   const dyld3::MachOAnalyzer::VMAddrConverter& vmAddrConverter) {
            __block Node protocolsNode;

            auto visitProtocol = ^(uint64_t protocolVMAddr, const dyld3::MachOAnalyzer::ObjCProtocol& protocol) {
                if (const char *name = getString(ma, protocol.nameVMAddr)) {
                    protocolsNode.array.push_back(Node{name});
                }
            };

            ma->forEachObjCProtocol(protocolsVMAddr, vmAddrConverter, visitProtocol);

            return protocolsNode.array.empty() ? std::optional<Node>() : protocolsNode;
        };

        auto getProtocols = ^(const dyld3::MachOAnalyzer* ma,
                              const dyld3::MachOAnalyzer::VMAddrConverter& vmAddrConverter) {
            __block Node protocols;

            auto getMethods = ^(const dyld3::MachOAnalyzer* mh, uint64_t methodListVMAddr, const std::string &prefix, Node &node){
                auto visitMethod = ^(uint64_t methodVMAddr, const dyld3::MachOAnalyzer::ObjCMethod& method,
                                     bool& stopMethod) {
                    if (auto name = getString(mh, method.nameVMAddr)) {
                        node.array.push_back(Node{prefix + name});
                    }
                };

                ma->forEachObjCMethod(methodListVMAddr, vmAddrConverter, sharedCacheRelativeSelectorBaseVMAddress, visitMethod);
            };

            auto visitProtocol = ^(uint64_t protoVMAddr,
                                   const dyld3::MachOAnalyzer::ObjCProtocol& objcProto,
                                   bool& stopProtocol) {
                const char* protoName = getString(ma, objcProto.nameVMAddr);
                if (!protoName)
                    return;

                Node entry;
                entry.map["protocolName"] = Node{protoName};

                if ( objcProto.protocolsVMAddr != 0 ) {
                    __block Node visitedProtocols;

                    auto visitProtocolInner = ^(uint64_t protocolRefVMAddr, const dyld3::MachOAnalyzer::ObjCProtocol& protocol) {
                        if (auto name = getString(ma, protocol.nameVMAddr)) {
                            visitedProtocols.array.push_back(Node{name});
                        }
                    };

                    ma->forEachObjCProtocol(objcProto.protocolsVMAddr, vmAddrConverter, visitProtocolInner);
                    if (!visitedProtocols.array.empty()) {
                        entry.map["protocols"] = visitedProtocols;
                    }
                }

                Node methods;
                getMethods(ma, objcProto.instanceMethodsVMAddr, instancePrefix, methods);
                getMethods(ma, objcProto.classMethodsVMAddr, classPrefix, methods);
                if (!methods.array.empty()) {
                    entry.map["methods"] = methods;
                }

                Node optMethods;
                getMethods(ma, objcProto.optionalInstanceMethodsVMAddr, instancePrefix, optMethods);
                getMethods(ma, objcProto.optionalClassMethodsVMAddr, classPrefix, optMethods);
                if (!optMethods.array.empty()) {
                    entry.map["optionalMethods"] = optMethods;
                }

                protocols.array.push_back(entry);
            };

            Diagnostics diag;
            ma->forEachObjCProtocol(diag, vmAddrConverter, visitProtocol);

            return protocols.array.empty() ? std::optional<Node>() : protocols;
        };

        auto getSelRefs = ^(const dyld3::MachOAnalyzer* ma,
                            const dyld3::MachOAnalyzer::VMAddrConverter& vmAddrConverter) {
            __block std::vector<const char *> selNames;

            auto visitSelRef = ^(uint64_t selRefVMAddr, uint64_t selRefTargetVMAddr, bool& stop) {
                if (auto selValue = getString(ma, selRefTargetVMAddr)) {
                    selNames.push_back(selValue);
                }
            };

            Diagnostics diag;
            ma->forEachObjCSelectorReference(diag, vmAddrConverter, visitSelRef);

            std::sort(selNames.begin(), selNames.end(),
                      [](const char* a, const char* b) {
                return strcasecmp(a, b) < 0;
            });

            Node selrefs;
            for (auto s: selNames) {
                selrefs.array.push_back(Node{s});
            }

            return selrefs.array.empty() ? std::optional<Node>() : selrefs;
        };

        auto getClasses = ^(const dyld3::MachOAnalyzer* ma,
                            const dyld3::MachOAnalyzer::VMAddrConverter& vmAddrConverter) {
            Diagnostics diag;
            const uint32_t pointerSize = ma->pointerSize();

            // Get the vmAddrs for all exported symbols as we want to know if classes
            // are exported
            std::set<uint64_t> exportedSymbolVMAddrs;
            {
                uint64_t loadAddress = ma->preferredLoadAddress();

                uint32_t exportTrieRuntimeOffset;
                uint32_t exportTrieSize;
                if ( ma->hasExportTrie(exportTrieRuntimeOffset, exportTrieSize) ) {
                    const uint8_t* start = (uint8_t*)ma + exportTrieRuntimeOffset;
                    const uint8_t* end = start + exportTrieSize;
                    std::vector<ExportInfoTrie::Entry> exports;
                    if ( ExportInfoTrie::parseTrie(start, end, exports) ) {
                        for (const ExportInfoTrie::Entry& entry: exports) {
                            exportedSymbolVMAddrs.insert(loadAddress + entry.info.address);
                        }
                    }
                }
            }

            __block Node classesNode;
            __block bool skippedPreviousClass = false;
            auto visitClass = ^(uint64_t classVMAddr,
                                uint64_t classSuperclassVMAddr, uint64_t classDataVMAddr,
                                const dyld3::MachOAnalyzer::ObjCClassInfo& objcClass, bool isMetaClass,
                                bool& stopClass) {
                if (isMetaClass) {
                    if (skippedPreviousClass) {
                        // If the class was bad, then skip the meta class too
                        skippedPreviousClass = false;
                        return;
                    }
                } else {
                    skippedPreviousClass = true;
                }

                std::string classType = "-";
                if (isMetaClass)
                    classType = "+";
                dyld3::MachOAnalyzer::PrintableStringResult classNameResult;
                const char* className = ma->getPrintableString(objcClass.nameVMAddr(pointerSize), classNameResult);
                if (classNameResult != dyld3::MachOAnalyzer::PrintableStringResult::CanPrint) {
                    return;
                }

                const char* superClassName = nullptr;
                if ( DyldSharedCache::inDyldCache(dyldCache, ma) ) {
                    if ( objcClass.superclassVMAddr != 0 ) {
                        if (isMetaClass) {
                            // If we are root class, then our superclass should actually point to our own class
                            const uint32_t RO_ROOT = (1<<1);
                            if ( objcClass.flags(pointerSize) & RO_ROOT ) {
                                auto it = classVMAddrToName.find(objcClass.superclassVMAddr);
                                assert(it != classVMAddrToName.end());
                                superClassName = it->second;
                            } else {
                                auto it = metaclassVMAddrToName.find(objcClass.superclassVMAddr);
                                assert(it != metaclassVMAddrToName.end());
                                superClassName = it->second;
                            }
                        } else {
                            auto it = classVMAddrToName.find(objcClass.superclassVMAddr);
                            assert(it != classVMAddrToName.end());
                            superClassName = it->second;
                        }
                    }
                } else {
                    // On-disk binary.  Lets crack the chain to work out what we are pointing at
                    dyld3::MachOAnalyzer::ChainedFixupPointerOnDisk fixup;
                    if ( pointerSize == 8 )
                        fixup.raw64 = objcClass.superclassVMAddr;
                    else
                        fixup.raw32 = (uint32_t)objcClass.superclassVMAddr;
                    uint32_t  bindOrdinal;
                    int64_t   embeddedAddend;
                    if (fixup.isBind(vmAddrConverter.chainedPointerFormat, bindOrdinal, embeddedAddend)) {
                        // Bind to another image.  Use the bind table to work out which name to bind to
                        const char* symbolName = onDiskChainedFixupBindTargets[(size_t)bindOrdinal];
                        if (isMetaClass) {
                            if ( strstr(symbolName, "_OBJC_METACLASS_$_") == symbolName ) {
                                superClassName = symbolName + strlen("_OBJC_METACLASS_$_");
                            } else {
                                // Swift classes don't start with these prefixes so just skip them
                                if (objcClass.isSwiftLegacy || objcClass.isSwiftStable)
                                    return;
                            }
                        } else {
                            if ( strstr(symbolName, "_OBJC_CLASS_$_") == symbolName ) {
                                superClassName = symbolName + strlen("_OBJC_CLASS_$_");
                            } else {
                                // Swift classes don't start with these prefixes so just skip them
                                if (objcClass.isSwiftLegacy || objcClass.isSwiftStable)
                                    return;
                            }
                        }
                    } else {
                        // Rebase within this image.
                        if (isMetaClass) {
                            auto it = onDiskMetaclassVMAddrToName.find(objcClass.superclassVMAddr);
                            assert(it != onDiskMetaclassVMAddrToName.end());
                            superClassName = it->second;
                        } else {
                            auto it = onDiskClassVMAddrToName.find(objcClass.superclassVMAddr);
                            assert(it != onDiskClassVMAddrToName.end());
                            superClassName = it->second;
                        }
                    }
                }

                // Print the methods on this class
                __block Node methodsNode;
                auto visitMethod = ^(uint64_t methodVMAddr, const dyld3::MachOAnalyzer::ObjCMethod& method, bool& stopMethod) {
                    dyld3::MachOAnalyzer::PrintableStringResult methodNameResult;
                    const char* methodName = ma->getPrintableString(method.nameVMAddr, methodNameResult);
                    if (methodNameResult != dyld3::MachOAnalyzer::PrintableStringResult::CanPrint)
                        return;
                    methodsNode.array.push_back(Node{classType + methodName});
                };
                ma->forEachObjCMethod(objcClass.baseMethodsVMAddr(pointerSize), vmAddrConverter,
                                      sharedCacheRelativeSelectorBaseVMAddress, visitMethod);

                std::optional<Node> properties = getProperties(ma, objcClass.basePropertiesVMAddr(pointerSize), vmAddrConverter);

                if (isMetaClass) {
                    assert(!classesNode.array.empty());
                    Node& currentClassNode = classesNode.array.back();
                    assert(currentClassNode.map["className"].value == className);
                    if (!methodsNode.array.empty()) {
                        Node& currentMethodsNode = currentClassNode.map["methods"];
                        currentMethodsNode.array.insert(currentMethodsNode.array.end(),
                                                        methodsNode.array.begin(),
                                                        methodsNode.array.end());
                    }
                    if (properties.has_value()) {
                        Node& currentPropertiesNode = currentClassNode.map["properties"];
                        currentPropertiesNode.array.insert(currentPropertiesNode.array.end(),
                                                           properties->array.begin(),
                                                           properties->array.end());
                    }
                    return;
                }

                Node currentClassNode;
                currentClassNode.map["className"] = Node{className};
                if ( superClassName != nullptr )
                    currentClassNode.map["superClassName"] = Node{superClassName};
                if (!methodsNode.array.empty())
                    currentClassNode.map["methods"] = methodsNode;
                if (properties.has_value())
                    currentClassNode.map["properties"] = properties.value();
                if (std::optional<Node> protocols = getClassProtocols(ma, objcClass.baseProtocolsVMAddr(pointerSize), vmAddrConverter))
                    currentClassNode.map["protocols"] = protocols.value();

                currentClassNode.map["exported"] = Node{exportedSymbolVMAddrs.count(classVMAddr) != 0};

                // We didn't skip this class so mark it as such
                skippedPreviousClass = false;

                classesNode.array.push_back(currentClassNode);
            };

            ma->forEachObjCClass(diag, vmAddrConverter, visitClass);
            return classesNode.array.empty() ? std::optional<Node>() : classesNode;
        };

        auto getCategories = ^(const dyld3::MachOAnalyzer* ma,
                               const dyld3::MachOAnalyzer::VMAddrConverter& vmAddrConverter) {
            Diagnostics diag;

            const uint32_t pointerSize = ma->pointerSize();

            __block Node categoriesNode;
            auto visitCategory = ^(uint64_t categoryVMAddr,
                                   const dyld3::MachOAnalyzer::ObjCCategory& objcCategory,
                                   bool& stopCategory) {
                dyld3::MachOAnalyzer::PrintableStringResult categoryNameResult;
                const char* categoryName = ma->getPrintableString(objcCategory.nameVMAddr, categoryNameResult);
                if (categoryNameResult != dyld3::MachOAnalyzer::PrintableStringResult::CanPrint)
                    return;

                    const char* className = nullptr;
                    if ( DyldSharedCache::inDyldCache(dyldCache, ma) ) {
                        // The class might be missing if the target is not in the shared cache.  So just skip these ones
                        if ( objcCategory.clsVMAddr == 0 )
                            return;

                        auto it = classVMAddrToName.find(objcCategory.clsVMAddr);
                        if (it == classVMAddrToName.end()) {
                            // This is an odd binary with perhaps a Swift class.  Just skip this entry
                            // Specifically, categories can be attached to "stub classes" which are not in the
                            // objc class list.  Instead the ISA (really the ISA + 8" of the class the category is
                            // attached to, is listed in a section called __objc_stublist.  Those are Swift stub classes
                            return;
                        }
                        className = it->second;
                    } else {
                        // On-disk binary.  Lets crack the chain to work out what we are pointing at
                        dyld3::MachOAnalyzer::ChainedFixupPointerOnDisk fixup;
                        fixup.raw64 = objcCategory.clsVMAddr;
                        if ( pointerSize == 8 )
                            fixup.raw64 = objcCategory.clsVMAddr;
                        else
                            fixup.raw32 = (uint32_t)objcCategory.clsVMAddr;
                        uint32_t  bindOrdinal;
                        int64_t   embeddedAddend;
                        if (fixup.isBind(vmAddrConverter.chainedPointerFormat, bindOrdinal, embeddedAddend)) {
                            // Bind to another image.  Use the bind table to work out which name to bind to
                            const char* symbolName = onDiskChainedFixupBindTargets[(size_t)bindOrdinal];
                            if ( strstr(symbolName, "_OBJC_CLASS_$_") == symbolName ) {
                                className = symbolName + strlen("_OBJC_CLASS_$_");
                            } else {
                                // Swift classes don't start with these prefixes so just skip them
                                // We don't know that this is a Swift class/category though, but skip it anyway
                                return;
                            }
                        } else {
                            auto it = onDiskClassVMAddrToName.find(objcCategory.clsVMAddr);
                            if (it == onDiskClassVMAddrToName.end()) {
                                // This is an odd binary with perhaps a Swift class.  Just skip this entry
                                return;
                            }
                            className = it->second;
                        }
                    }

                // Print the instance methods on this category
                __block Node methodsNode;
                auto visitInstanceMethod = ^(uint64_t methodVMAddr, const dyld3::MachOAnalyzer::ObjCMethod& method, bool& stopMethod) {
                    if (auto methodName = getString(ma, method.nameVMAddr))
                        methodsNode.array.push_back(Node{instancePrefix + methodName});
                };
                ma->forEachObjCMethod(objcCategory.instanceMethodsVMAddr, vmAddrConverter,
                                      sharedCacheRelativeSelectorBaseVMAddress, visitInstanceMethod);

                // Print the instance methods on this category
                __block Node classMethodsNode;
                auto visitClassMethod = ^(uint64_t methodVMAddr, const dyld3::MachOAnalyzer::ObjCMethod& method, bool& stopMethod) {
                    if (auto methodName = getString(ma, method.nameVMAddr))
                        methodsNode.array.push_back(Node{classPrefix + methodName});
                };
                ma->forEachObjCMethod(objcCategory.classMethodsVMAddr, vmAddrConverter,
                                      sharedCacheRelativeSelectorBaseVMAddress, visitClassMethod);

                Node currentCategoryNode;
                currentCategoryNode.map["categoryName"] = Node{categoryName};
                currentCategoryNode.map["className"] = Node{className};
                if (!methodsNode.array.empty())
                    currentCategoryNode.map["methods"] = methodsNode;
                if (std::optional<Node> properties = getProperties(ma, objcCategory.instancePropertiesVMAddr, vmAddrConverter))
                    currentCategoryNode.map["properties"] = properties.value();
                if (std::optional<Node> protocols = getClassProtocols(ma, objcCategory.protocolsVMAddr, vmAddrConverter))
                    currentCategoryNode.map["protocols"] = protocols.value();

                categoriesNode.array.push_back(currentCategoryNode);
            };

            ma->forEachObjCCategory(diag, vmAddrConverter, visitCategory);
            return categoriesNode.array.empty() ? std::optional<Node>() : categoriesNode;
        };

        __block bool needsComma = false;

        dyld3::json::streamArrayBegin(needsComma);

        dyldCache->forEachImage(^(const mach_header *mh, const char *installName) {
            dyld3::MachOAnalyzer::VMAddrConverter vmAddrConverter = dyldCache->makeVMAddrConverter(rebased);
            const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)mh;

            Node imageRecord;
            imageRecord.map["imagePath"] = Node{installName};
            imageRecord.map["imageType"] = Node{"cache-dylib"};
            std::optional<Node> classes = getClasses(ma, vmAddrConverter);
            std::optional<Node> categories = getCategories(ma, vmAddrConverter);
            std::optional<Node> protocols = getProtocols(ma, vmAddrConverter);
            std::optional<Node> selrefs = getSelRefs(ma, vmAddrConverter);

            // Skip emitting images with no objc data
            if (!classes.has_value() && !categories.has_value() && !protocols.has_value() && !selrefs.has_value())
                return;
            if (classes.has_value())
                imageRecord.map["classes"] = classes.value();
            if (categories.has_value())
                imageRecord.map["categories"] = categories.value();
            if (protocols.has_value())
                imageRecord.map["protocols"] = protocols.value();
            if (selrefs.has_value())
                imageRecord.map["selrefs"] = selrefs.value();

            dyld3::json::streamArrayNode(needsComma, imageRecord);
        });

        const dyld3::MachOAnalyzer* mainMA = nullptr;
        if ( dyldCache ) {
            // gracefully handling older dyld caches
            if ( dyldCache->header.mappingOffset < 0x170 ) {
                fprintf(stderr, "dyld_closure_util: can't operate against an old (pre-dyld4) dyld cache\n");
                exit(1);
            }

            // HACK: use libSystem.dylib from cache as main executable to bootstrap state
            uint32_t imageIndex;
            if ( dyldCache->hasImagePath("/usr/lib/libSystem.B.dylib", imageIndex) ) {
                uint64_t ignore1;
                uint64_t ignore2;
                mainMA = (MachOAnalyzer*)dyldCache->getIndexedImageEntry(imageIndex, ignore1, ignore2);
            }
        }

        KernelArgs            kernArgs(mainMA, {"test.exe"}, {}, {});
        SyscallDelegate       osDelegate;
        osDelegate._dyldCache   = dyldCache;
        osDelegate._rootPath    = options.rootPath;

        __block ProcessConfig config(&kernArgs, osDelegate);
        RuntimeState stateObject(config);
        RuntimeState& state = stateObject;

        config.dyldCache.addr->forEachLaunchLoaderSet(^(const char* executableRuntimePath, const PrebuiltLoaderSet* pbls) {

            __block Diagnostics diag;
            bool                checkIfOSBinary = state.config.process.archs->checksOSBinary();
            state.config.syscall.withReadOnlyMappedFile(diag, executableRuntimePath, checkIfOSBinary, ^(const void* mapping, size_t mappedSize, bool isOSBinary, const FileID& fileID, const char* canonicalPath) {
                if ( const dyld3::MachOFile* mf = dyld3::MachOFile::compatibleSlice(diag, mapping, mappedSize, executableRuntimePath, state.config.process.platform, isOSBinary, *state.config.process.archs) ) {
                    const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)mf;
                    uint32_t pointerSize = ma->pointerSize();

                    // Populate the bind targets for classes from other images
                    onDiskChainedFixupBindTargets.clear();
                    ma->forEachChainedFixupTarget(diag, ^(int libOrdinal, const char* symbolName, uint64_t addend, bool weakImport, bool& stop) {
                        onDiskChainedFixupBindTargets.push_back(symbolName);
                    });
                    if ( diag.hasError() )
                        return;

                    // Populate the rebase targets for class names
                    onDiskMetaclassVMAddrToName.clear();
                    onDiskClassVMAddrToName.clear();
                    auto visitClass = ^(uint64_t classVMAddr,
                                        uint64_t classSuperclassVMAddr, uint64_t classDataVMAddr,
                                        const dyld3::MachOAnalyzer::ObjCClassInfo& objcClass, bool isMetaClass,
                                        bool& stopClass) {
                        if (auto className = getString(ma, objcClass.nameVMAddr(pointerSize))) {
                            if (isMetaClass)
                                onDiskMetaclassVMAddrToName[classVMAddr] = className;
                            else
                                onDiskClassVMAddrToName[classVMAddr] = className;
                        }
                    };

                    // Get a vmAddrConverter for this on-disk binary.  We can't use the shared cache one
                    dyld3::MachOAnalyzer::VMAddrConverter onDiskVMAddrConverter = ma->makeVMAddrConverter(rebased);

                    ma->forEachObjCClass(diag, onDiskVMAddrConverter, visitClass);

                    Node imageRecord;
                    imageRecord.map["imagePath"] = Node{executableRuntimePath};
                    imageRecord.map["imageType"] = Node{"executable"};
                    std::optional<Node> classes = getClasses(ma, onDiskVMAddrConverter);
                    std::optional<Node> categories = getCategories(ma, onDiskVMAddrConverter);
                    // TODO: protocols
                    std::optional<Node> selrefs = getSelRefs(ma, onDiskVMAddrConverter);

                    // Skip emitting images with no objc data
                    if (!classes.has_value() && !categories.has_value() && !selrefs.has_value())
                        return;
                    if (classes.has_value())
                        imageRecord.map["classes"] = classes.value();
                    if (categories.has_value())
                        imageRecord.map["categories"] = categories.value();
                    if (selrefs.has_value())
                        imageRecord.map["selrefs"] = selrefs.value();

                    dyld3::json::streamArrayNode(needsComma, imageRecord);
                }
            });
        });

        dyld3::json::streamArrayEnd(needsComma);
    }
    else if ( options.mode == modeObjCSelectors ) {
        if ( dyldCache->objcOpt() == nullptr ) {
            fprintf(stderr, "Error: could not get optimized objc\n");
            return 1;
        }
        const objc::SelectorHashTable* selectors = dyldCache->objcOpt()->selectorOpt();
        if ( selectors == nullptr ) {
            fprintf(stderr, "Error: could not get optimized objc selectors\n");
            return 1;
        }

        __block std::vector<const char*> selNames;
        selectors->forEachString(^(const char *str) {
            selNames.push_back(str);
        });

        std::sort(selNames.begin(), selNames.end(),
                  [](const char* a, const char* b) {
            // Sort by offset, not string value
            return a < b;
        });

        dyld3::json::Node root;
        for (const char* selName : selNames) {
            dyld3::json::Node selNode;
            selNode.map["selectorName"] = dyld3::json::Node{selName};
            selNode.map["offset"] = dyld3::json::Node{(int64_t)selName - (int64_t)dyldCache};

            root.array.push_back(selNode);
        }

        dyld3::json::printJSON(root, 0, std::cout);
    }
    else if ( options.mode == modeSwiftProtocolConformances ) {
#if 0
        // This would dump the conformances in each binary, not the table in the shared cache
        dyldCache->forEachImage(^(const mach_header *mh, const char *installName) {
            const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)mh;

            Diagnostics diag;
            bool contentRebased = (sharedCachePath == nullptr);
            auto vmAddrConverter = dyldCache->makeVMAddrConverter(contentRebased);
            uint64_t binaryCacheOffset = (uint64_t)ma - (uint64_t)dyldCache;
            ma->forEachSwiftProtocolConformance(diag, vmAddrConverter,
                                                ^(uint64_t protocolConformanceRuntimeOffset, const dyld3::MachOAnalyzer::SwiftProtocolConformance &protocolConformance, bool &stopProtocolConformance) {
                printf("(0x%08llx, 0x%08llx) -> 0x%08llx  %s\n",
                       binaryCacheOffset + protocolConformance.typeConformanceRuntimeOffset, binaryCacheOffset + protocolConformance.protocolRuntimeOffset,
                       binaryCacheOffset + protocolConformanceRuntimeOffset, installName);
            });
        });
#endif

        auto getLibraryLeafName = [](const char* path)
        {
            const char* start = strrchr(path, '/');
            if ( start != NULL )
                return &start[1];
            else
                return path;
        };

        // Find all the symbols.  This maps from VM Addresses to symbol name
        __block std::unordered_map<uint64_t, std::string_view> symbols;
        __block std::unordered_map<uint64_t, std::string_view> dylibs;
        dyldCache->forEachImage(^(const mach_header *mh, const char *installName) {
            const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)mh;
            Diagnostics diag;
            ma->forEachGlobalSymbol(diag, ^(const char *symbolName, uint64_t n_value, uint8_t n_type, uint8_t n_sect, uint16_t n_desc, bool &stop) {
                symbols[n_value] = symbolName;
                dylibs[n_value] = getLibraryLeafName(installName);
            });
            ma->forEachLocalSymbol(diag, ^(const char *symbolName, uint64_t n_value, uint8_t n_type, uint8_t n_sect, uint16_t n_desc, bool &stop) {
                symbols[n_value] = symbolName;
                dylibs[n_value] = getLibraryLeafName(installName);
            });
        });

#if 0
        // FIXME: Move to the new code for unmapped locals
        if ( (sharedCachePath != nullptr) && dyldCache->hasLocalSymbolsInfo() ) {
            // When mapping the cache from disk, we can also get the unmapped locals
            struct stat statbuf;
            if ( ::stat(sharedCachePath, &statbuf) ) {
                fprintf(stderr, "Error: stat failed for dyld shared cache at %s\n", sharedCachePath);
                return 1;
            }

            int cache_fd = ::open(sharedCachePath, O_RDONLY);
            if (cache_fd < 0) {
                fprintf(stderr, "Error: failed to open shared cache file at %s\n", sharedCachePath);
                return 1;
            }

            const void* mappedData = ::mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, cache_fd, 0);
            if ( mappedData == MAP_FAILED ) {
                fprintf(stderr, "Error: Shared cache not mmap()able\b");
                ::close(cache_fd);
                return 1;
            }
            ::close(cache_fd);

            const DyldSharedCache* localsCache = (const DyldSharedCache*)mappedData;

            const bool is64 = (strstr(localsCache->archName(), "64") != NULL);
            const uint32_t nlistFileOffset = (uint32_t)((uint8_t*)localsCache->getLocalNlistEntries() - (uint8_t*)dyldCache);
            const char* localStrings = localsCache->getLocalStrings();

            __block uint32_t entriesCount = 0;
            localsCache->forEachLocalSymbolEntry(^(uint32_t dylibOffset, uint32_t nlistStartIndex, uint32_t nlistCount, bool &stop) {
                const char* imageName = dyldCache->getIndexedImagePath(entriesCount);
                //printf("   nlistStartIndex=%5d, nlistCount=%5d, image=%s\n", nlistStartIndex, nlistCount, imageName);
                if ( is64 ) {
                    const nlist_64* symTab = (nlist_64*)((char*)dyldCache + nlistFileOffset);
                    for (int e = 0; e < nlistCount; ++e) {
                        const nlist_64* entry = &symTab[nlistStartIndex + e];
                        //printf("     nlist[%d].str=%d, %s\n", e, entry->n_un.n_strx, &localStrings[entry->n_un.n_strx]);
                        //printf("     nlist[%d].value=0x%0llX\n", e, entry->n_value);
                        symbols[entry->n_value] = &localStrings[entry->n_un.n_strx];
                        dylibs[entry->n_value] = getLibraryLeafName(imageName);
                    }
                }
                entriesCount++;
            });
        }
#endif

        auto getDylibForAddress = ^(uint64_t vmAddress) {
            __block std::string_view dylibName;
            dyldCache->forEachImage(^(const mach_header *mh, const char *installName) {
                if ( !dylibName.empty() )
                    return;
                const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)mh;
                ma->forEachSegment(^(const dyld3::MachOAnalyzer::SegmentInfo &info, bool &stop) {
                    if ( (vmAddress >= info.vmAddr) && (vmAddress < (info.vmAddr+info.vmSize)) ) {
                        dylibName = installName;
                        stop = true;
                    }
                });
            });
            return dylibName;
        };

        uint64_t cacheBaseAddress = dyldCache->unslidLoadAddress();

        const SwiftOptimizationHeader* swiftOptHeader = dyldCache->swiftOpt();
        if ( swiftOptHeader == nullptr ) {
            printf("No Swift optimization information present\n");
            return 0;
        }
        printf("Swift optimization version: %d\n", swiftOptHeader->version);
        if ( swiftOptHeader->version == 1 ) {
            printf("Type hash table\n");
            const SwiftHashTable* typeHashTable = (const SwiftHashTable*)((uint8_t*)dyldCache + swiftOptHeader->typeConformanceHashTableCacheOffset);
            typeHashTable->forEachValue(^(uint32_t bucketIndex, const dyld3::Array<SwiftTypeProtocolConformanceLocation>& impls) {
                for (const SwiftTypeProtocolConformanceLocation& protoLoc : impls) {
                    std::string_view typeDesc = "n/a";
                    std::string_view typeDescDylib;
                    if ( auto it = symbols.find(protoLoc.typeDescriptorCacheOffset + cacheBaseAddress); it != symbols.end() ) {
                        typeDesc = it->second;
                        typeDescDylib = dylibs[protoLoc.typeDescriptorCacheOffset + cacheBaseAddress];
                    } else {
                        typeDescDylib = getDylibForAddress(protoLoc.typeDescriptorCacheOffset + cacheBaseAddress);
                        if ( typeDescDylib.empty() )
                            typeDescDylib = "n/a";
                        else
                            typeDescDylib = getLibraryLeafName(typeDescDylib.data());
                    }

                    std::string_view protocol = "n/a";
                    std::string_view protocolDylib;
                    if ( auto it = symbols.find(protoLoc.protocolCacheOffset + cacheBaseAddress); it != symbols.end() ) {
                        protocol = it->second;
                        protocolDylib = dylibs[protoLoc.protocolCacheOffset + cacheBaseAddress];
                    } else {
                        protocolDylib = getDylibForAddress(protoLoc.protocolCacheOffset + cacheBaseAddress);
                        if ( protocolDylib.empty() )
                            protocolDylib = "n/a";
                        else
                            protocolDylib = getLibraryLeafName(protocolDylib.data());
                    }

                    std::string_view conformance = "n/a";
                    std::string_view conformanceDylib;
                    if ( auto it = symbols.find(protoLoc.protocolConformanceCacheOffset + cacheBaseAddress); it != symbols.end() ) {
                        conformance = it->second;
                        conformanceDylib = dylibs[protoLoc.protocolConformanceCacheOffset + cacheBaseAddress];
                    } else {
                        conformanceDylib = getDylibForAddress(protoLoc.protocolConformanceCacheOffset + cacheBaseAddress);
                        if ( conformanceDylib.empty() )
                            conformanceDylib = "n/a";
                        else
                            conformanceDylib = getLibraryLeafName(conformanceDylib.data());
                    }

                    printf("[%d]: (type: %s (cache offset 0x%llx) in %s, protocol %s (cache offset 0x%llx) in %s) -> (conformance: %s (cache offset 0x%llx) in %s)\n",
                           bucketIndex,
                           typeDesc.data(), protoLoc.typeDescriptorCacheOffset, typeDescDylib.data(),
                           protocol.data(), protoLoc.protocolCacheOffset, protocolDylib.data(),
                           conformance.data(), protoLoc.protocolConformanceCacheOffset, conformanceDylib.data());
                }
            });

            printf("Metadata hash table\n");
            const SwiftHashTable* metadataHashTable = (const SwiftHashTable*)((uint8_t*)dyldCache + swiftOptHeader->metadataConformanceHashTableCacheOffset);
            metadataHashTable->forEachValue(^(uint32_t bucketIndex, const dyld3::Array<SwiftMetadataProtocolConformanceLocation>& impls) {
                for (const SwiftMetadataProtocolConformanceLocation& protoLoc : impls) {
                    std::string_view metadataDesc = "n/a";
                    std::string_view metadataDescDylib;
                    if ( auto it = symbols.find(protoLoc.metadataCacheOffset + cacheBaseAddress); it != symbols.end() ) {
                        metadataDesc = it->second;
                        metadataDescDylib = dylibs[protoLoc.metadataCacheOffset + cacheBaseAddress];
                    } else {
                        metadataDescDylib = getDylibForAddress(protoLoc.metadataCacheOffset + cacheBaseAddress);
                        if ( metadataDescDylib.empty() )
                            metadataDescDylib = "n/a";
                        else
                            metadataDescDylib = getLibraryLeafName(metadataDescDylib.data());
                    }

                    std::string_view protocol = "n/a";
                    std::string_view protocolDylib;
                    if ( auto it = symbols.find(protoLoc.protocolCacheOffset + cacheBaseAddress); it != symbols.end() ) {
                        protocol = it->second;
                        protocolDylib = dylibs[protoLoc.protocolCacheOffset + cacheBaseAddress];
                    } else {
                        protocolDylib = getDylibForAddress(protoLoc.protocolCacheOffset + cacheBaseAddress);
                        if ( protocolDylib.empty() )
                            protocolDylib = "n/a";
                        else
                            protocolDylib = getLibraryLeafName(protocolDylib.data());
                    }

                    std::string_view conformance = "n/a";
                    std::string_view conformanceDylib;
                    if ( auto it = symbols.find(protoLoc.protocolConformanceCacheOffset + cacheBaseAddress); it != symbols.end() ) {
                        conformance = it->second;
                        conformanceDylib = dylibs[protoLoc.protocolConformanceCacheOffset + cacheBaseAddress];
                    } else {
                        conformanceDylib = getDylibForAddress(protoLoc.protocolConformanceCacheOffset + cacheBaseAddress);
                        if ( conformanceDylib.empty() )
                            conformanceDylib = "n/a";
                        else
                            conformanceDylib = getLibraryLeafName(conformanceDylib.data());
                    }

                    printf("[%d]: (metadata: %s (cache offset 0x%llx) in %s, protocol %s (cache offset 0x%llx) in %s) -> (conformance: %s (cache offset 0x%llx) in %s)\n",
                           bucketIndex,
                           metadataDesc.data(), protoLoc.metadataCacheOffset, metadataDescDylib.data(),
                           protocol.data(), protoLoc.protocolCacheOffset, protocolDylib.data(),
                           conformance.data(), protoLoc.protocolConformanceCacheOffset, conformanceDylib.data());
                }
            });

            printf("Foreign type hash table\n");
            const SwiftHashTable* foreignTypeHashTable = (const SwiftHashTable*)((uint8_t*)dyldCache + swiftOptHeader->foreignTypeConformanceHashTableCacheOffset);
            foreignTypeHashTable->forEachValue(^(uint32_t bucketIndex, const dyld3::Array<SwiftForeignTypeProtocolConformanceLocation>& impls) {
                for (const SwiftForeignTypeProtocolConformanceLocation& protoLoc : impls) {
                    std::string_view typeNameView((const char*)dyldCache + protoLoc.foreignDescriptorNameCacheOffset, protoLoc.foreignDescriptorNameLength);
                    std::string typeName;
                    if ( typeNameView.size() != strlen(typeNameView.data()) ) {
                        typeName.reserve(typeNameView.size());
                        for (const char c : typeNameView) {
                            if ( c == '\0' )
                                typeName += "\\0";
                            else
                                typeName += c;
                        }
                        typeNameView = typeName;
                    }
;
                    std::string_view protocol = "n/a";
                    std::string_view protocolDylib;
                    if ( auto it = symbols.find(protoLoc.protocolCacheOffset + cacheBaseAddress); it != symbols.end() ) {
                        protocol = it->second;
                        protocolDylib = dylibs[protoLoc.protocolCacheOffset + cacheBaseAddress];
                    } else {
                        protocolDylib = getDylibForAddress(protoLoc.protocolCacheOffset + cacheBaseAddress);
                        if ( protocolDylib.empty() )
                            protocolDylib = "n/a";
                        else
                            protocolDylib = getLibraryLeafName(protocolDylib.data());
                    }

                    std::string_view conformance = "n/a";
                    std::string_view conformanceDylib;
                    if ( auto it = symbols.find(protoLoc.protocolConformanceCacheOffset + cacheBaseAddress); it != symbols.end() ) {
                        conformance = it->second;
                        conformanceDylib = dylibs[protoLoc.protocolConformanceCacheOffset + cacheBaseAddress];
                    } else {
                        conformanceDylib = getDylibForAddress(protoLoc.protocolConformanceCacheOffset + cacheBaseAddress);
                        if ( conformanceDylib.empty() )
                            conformanceDylib = "n/a";
                        else
                            conformanceDylib = getLibraryLeafName(conformanceDylib.data());
                    }

                    printf("[%d]: (type name: %s (cache offset 0x%llx), protocol %s (cache offset 0x%llx) in %s) -> (conformance: %s (cache offset 0x%llx) in %s)\n",
                           bucketIndex,
                           typeName.data(), protoLoc.foreignDescriptorNameCacheOffset,
                           protocol.data(), protoLoc.protocolCacheOffset, protocolDylib.data(),
                           conformance.data(), protoLoc.protocolConformanceCacheOffset, conformanceDylib.data());
                }
            });
        } else {
            printf("Unhandled version\n");
        }
    }
    else if ( options.mode == modeExtract ) {
        return dyld_shared_cache_extract_dylibs(sharedCachePath, options.extractionDir);
    }
    else if ( options.mode == modeObjCImpCaches ) {
        if (sharedCachePath == nullptr) {
            fprintf(stderr, "Cannot emit imp caches with live cache.  Run again with the path to the cache file\n");
            return 1;
        }
        __block std::map<uint64_t, const char*> methodToClassMap;
        __block std::map<uint64_t, const char*> classVMAddrToNameMap;
        const bool contentRebased = false;
        const uint32_t pointerSize = 8;

        // Get the base pointers from the magic section in objc
        __block uint64_t objcCacheOffsetsSize = 0;
        __block const void* objcCacheOffsets = nullptr;
        __block Diagnostics diag;
        dyldCache->forEachImage(^(const mach_header* mh, const char* installName) {
            if ( !strcmp(installName, "/usr/lib/libobjc.A.dylib") ) {
                const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)mh;
                objcCacheOffsets = ma->findSectionContent("__DATA_CONST", "__objc_scoffs", objcCacheOffsetsSize);
            }
        });

        if ( objcCacheOffsets == nullptr ) {
            fprintf(stderr, "Unable to print imp-caches as cannot find __DATA_CONST __objc_scoffs inside /usr/lib/libobjc.A.dylib\n");
            return 1;
        }

        if ( objcCacheOffsetsSize < (4 * pointerSize) ) {
            fprintf(stderr, "Unable to print imp-caches as __DATA_CONST __objc_scoffs is too small (%lld vs required %u)\n", objcCacheOffsetsSize, (4 * pointerSize));
            return 1;
        }

        dyld3::MachOAnalyzer::VMAddrConverter vmAddrConverter = dyldCache->makeVMAddrConverter(contentRebased);

        uint64_t selectorStringVMAddrStart  = vmAddrConverter.convertToVMAddr(((uint64_t*)objcCacheOffsets)[0]);
        uint64_t selectorStringVMAddrEnd    = vmAddrConverter.convertToVMAddr(((uint64_t*)objcCacheOffsets)[1]);

        uint64_t sharedCacheRelativeSelectorBaseVMAddress = dyldCache->sharedCacheRelativeSelectorBaseVMAddress();

        dyldCache->forEachImage(^(const mach_header *mh, const char *installName) {
            if (diag.hasError())
                return;

            const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)mh;
            intptr_t slide = ma->getSlide();

            auto visitClass = ^(uint64_t classVMAddr,
                                uint64_t classSuperclassVMAddr,
                                uint64_t classDataVMAddr,
                                const dyld3::MachOAnalyzer::ObjCClassInfo& objcClass,
                                bool isMetaClass,
                                bool& stopClass) {
                const char* className = (const char*)objcClass.nameVMAddr(pointerSize) + slide;
                classVMAddrToNameMap[classVMAddr] = className;
                ma->forEachObjCMethod(objcClass.baseMethodsVMAddr(pointerSize), vmAddrConverter,
                                      sharedCacheRelativeSelectorBaseVMAddress,
                                      ^(uint64_t methodVMAddr, const dyld3::MachOAnalyzer::ObjCMethod& method, bool& stopMethod) {
                    // const char* methodName = (const char*)(method.nameVMAddr + slide);
                    methodToClassMap[method.impVMAddr] = className;
                });
            };
            ma->forEachObjCClass(diag, vmAddrConverter, visitClass);

            auto visitCategory = ^(uint64_t categoryVMAddr, const dyld3::MachOAnalyzer::ObjCCategory& objcCategory, bool& stopCategory) {
                ma->forEachObjCMethod(objcCategory.instanceMethodsVMAddr, vmAddrConverter,
                                      sharedCacheRelativeSelectorBaseVMAddress,
                                      ^(uint64_t methodVMAddr, const dyld3::MachOAnalyzer::ObjCMethod& method, bool& stopMethod) {
                    const char* catName = (const char*)objcCategory.nameVMAddr + slide;
                    // const char* methodName = (const char*)(method.nameVMAddr + slide);
                    methodToClassMap[method.impVMAddr] = catName;
                });

                ma->forEachObjCMethod(objcCategory.classMethodsVMAddr, vmAddrConverter,
                                      sharedCacheRelativeSelectorBaseVMAddress,
                                      ^(uint64_t methodVMAddr, const dyld3::MachOAnalyzer::ObjCMethod& method, bool& stopMethod) {
                    const char* catName = (const char*)objcCategory.nameVMAddr + slide;
                    // const char* methodName = (const char*)(method.nameVMAddr + slide);
                    methodToClassMap[method.impVMAddr] = catName;
                });
            };
            ma->forEachObjCCategory(diag, vmAddrConverter, visitCategory);
        });
        if (diag.hasError())
            return 1;

        dyldCache->forEachImage(^(const mach_header *mh, const char *installName) {
            if (diag.hasError())
                return;

            const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)mh;
            intptr_t slide = ma->getSlide();

            auto visitClass = ^(uint64_t classVMAddr,
                                uint64_t classSuperclassVMAddr,
                                uint64_t classDataVMAddr,
                                const dyld3::MachOAnalyzer::ObjCClassInfo& objcClass,
                                bool isMetaClass,
                                bool& stopClass) {
                const char* type = "class";
                if (isMetaClass)
                    type = "meta-class";
                const char* className = (const char*)objcClass.nameVMAddr(pointerSize) + slide;

                if (objcClass.methodCacheVMAddr == 0) {
                    printf("%s (%s): empty\n", className, type);
                    return;
                }

                struct Bucket {
                    uint32_t selOffset;
                    uint32_t impOffset;
                };
                struct ImpCache {
                    int32_t  fallback_class_offset;
                    uint32_t cache_shift :  5;
                    uint32_t cache_mask  : 11;
                    uint32_t occupied    : 14;
                    uint32_t has_inlines :  1;
                    uint32_t bit_one     :  1;
                    struct Bucket buckets[];
                };

                const ImpCache* impCache = (const ImpCache*)(objcClass.methodCacheVMAddr + slide);
                printf("%s (%s): %d buckets\n", className, type, impCache->cache_mask + 1);

                if ((classVMAddr + impCache->fallback_class_offset) != objcClass.superclassVMAddr) {
                    printf("Flattening fallback: %s\n", classVMAddrToNameMap[classVMAddr + impCache->fallback_class_offset]);
                }
                // Buckets are a 32-bit offset from the impcache itself
                for (uint32_t i = 0; i <= impCache->cache_mask ; ++i) {
                    const Bucket& b = impCache->buckets[i];
                    uint64_t sel = (uint64_t)b.selOffset + selectorStringVMAddrStart;
                    uint64_t imp = classVMAddr - (uint64_t)b.impOffset;
                    if (b.selOffset == 0xFFFFFFFF) {
                        // Empty bucket
                        printf("  - 0x%016llx: %s\n", 0ULL, "");
                    } else {
                        assert(sel < selectorStringVMAddrEnd);

                        auto it = methodToClassMap.find(imp);
                        if (it == methodToClassMap.end()) {
                            fprintf(stderr, "Could not find IMP %llx (for %s)\n", imp, (const char*)(sel + slide));
                        }
                        assert(it != methodToClassMap.end());
                        printf("  - 0x%016llx: %s (from %s)\n", imp, (const char*)(sel + slide), it->second);
                    }
                }
           };
            ma->forEachObjCClass(diag, vmAddrConverter, visitClass);
        });
    } else {
        switch ( options.mode ) {
            case modeList: {
                // list all dylibs, including their aliases (symlinks to them) with option vmaddr
                __block std::vector<std::unordered_set<std::string>> indexToPaths;
                __block std::vector<uint64_t> indexToAddr;
                __block std::vector<std::string> indexToUUID;
                dyldCache->forEachImageTextSegment(^(uint64_t loadAddressUnslid, uint64_t textSegmentSize, const unsigned char* dylibUUID, const char* installName, bool& stop) {
                    std::unordered_set<std::string> empty;
                    if ( options.printVMAddrs )
                        indexToAddr.push_back(loadAddressUnslid);
                    if ( options.printUUIDs ) {
                        uuid_string_t uuidString;
                        uuid_unparse_upper(dylibUUID, uuidString);
                        indexToUUID.push_back(uuidString);
                    }
                    indexToPaths.push_back(empty);
                    indexToPaths.back().insert(installName);
                });
                dyldCache->forEachDylibPath(^(const char* dylibPath, uint32_t index) {
                    indexToPaths[index].insert(dylibPath);
                });
                int index = 0;
                for (const std::unordered_set<std::string>& paths : indexToPaths) {
                    for (const std::string& path: paths) {
                        if ( options.printVMAddrs )
                            printf("0x%08llX ", indexToAddr[index]);
                        if ( options.printUUIDs )
                             printf("<%s> ", indexToUUID[index].c_str());
                       printf("%s\n", path.c_str());
                    }
                    ++index;
                }
                break;
            }
            case modeListDylibsWithSection: {
                dyldCache->forEachImage(^(const mach_header* mh, const char* installName) {
                    dyld3::MachOFile* mf = (dyld3::MachOFile*)mh;
                    mf->forEachSection(^(const dyld3::MachOFile::SectionInfo& sectInfo, bool malformedSectionRange, bool& stop) {
                        if ( (strcmp(sectInfo.sectName, options.sectionName) == 0) && (strcmp(sectInfo.segInfo.segName, options.segmentName) == 0) ) {
                            printf("%s\n", installName);
                            stop = true;
                        }
                    });
                });
                break;
            }
            case modeMap: {
                __block std::map<uint64_t, const char*> dataSegNames;
                __block std::map<uint64_t, uint64_t>    dataSegEnds;
                dyldCache->forEachImage(^(const mach_header* mh, const char* installName) {
                    dyld3::MachOFile* mf = (dyld3::MachOFile*)mh;
                    mf->forEachSegment(^(const dyld3::MachOAnalyzer::SegmentInfo &info, bool &stop) {
                        printf("0x%08llX - 0x%08llX %s %s\n", info.vmAddr, info.vmAddr + info.vmSize, info.segName, installName);
                        if ( strncmp(info.segName, "__DATA", 6) == 0 ) {
                            dataSegNames[info.vmAddr] = installName;
                            dataSegEnds[info.vmAddr] = info.vmAddr + info.vmSize;
                        }
                    });
                });
                // <rdar://problem/51084507> Enhance dyld_shared_cache_util to show where section alignment added padding
                uint64_t lastEnd = 0;
                for (const auto& entry : dataSegEnds) {
                    uint64_t padding = entry.first - lastEnd;
                    if ( (padding > 32) && (lastEnd != 0) ) {
                        printf("0x%08llX - 0x%08llX PADDING %lluKB\n", lastEnd, entry.first, padding/1024);
                    }
                    lastEnd = entry.second;
                }
                break;
            }
            case modeDependencies: {
                __block bool dependentTargetFound = false;
                dyldCache->forEachImage(^(const mach_header* mh, const char* installName) {
                    if ( strcmp(options.dependentsOfPath, installName) != 0 )
                        return;
                    dependentTargetFound = true;

                    auto printDep = [&options](const char *loadPath, uint32_t compatVersion, uint32_t curVersion) {
                        if ( options.printDylibVersions ) {
                            uint32_t compat_vers = compatVersion;
                            uint32_t current_vers = curVersion;
                            printf("\t%s", loadPath);
                            if ( compat_vers != 0xFFFFFFFF ) {
                                printf("(compatibility version %u.%u.%u, current version %u.%u.%u)\n",
                                       (compat_vers >> 16),
                                       (compat_vers >> 8) & 0xff,
                                       (compat_vers) & 0xff,
                                       (current_vers >> 16),
                                       (current_vers >> 8) & 0xff,
                                       (current_vers) & 0xff);
                            }
                            else {
                                printf("\n");
                            }
                        }
                        else {
                            printf("\t%s\n", loadPath);
                        }
                    };

                    dyld3::MachOFile* mf = (dyld3::MachOFile*)mh;

                    // First print out our dylib and version.
                    const char* dylibInstallName;
                    uint32_t currentVersion;
                    uint32_t compatVersion;
                    if ( mf->getDylibInstallName(&dylibInstallName, &compatVersion, &currentVersion) ) {
                        printDep(dylibInstallName, compatVersion, currentVersion);
                    }

                    // Then the dependent dylibs.
                    mf->forEachDependentDylib(^(const char* depPath, bool isWeak, bool isReExport, bool isUpward, uint32_t cpatVersion, uint32_t curVersion, bool& stop) {
                        printDep(depPath, cpatVersion, curVersion);
                    });
                });
                if (options.dependentsOfPath && !dependentTargetFound) {
                    fprintf(stderr, "Error: could not find '%s' in the shared cache at\n  %s\n", options.dependentsOfPath, sharedCachePath);
                    exit(1);
                }
                break;
            }
            case modeLinkEdit: {
                std::map<uint32_t, const char*> pageToContent;
                auto add_linkedit = [&pageToContent](uint32_t pageStart, uint32_t pageEnd, const char* message) {
                    for (uint32_t p = pageStart; p <= pageEnd; p += 4096) {
                        std::map<uint32_t, const char*>::iterator pos = pageToContent.find(p);
                        if ( pos == pageToContent.end() ) {
                            pageToContent[p] = strdup(message);
                        }
                        else {
                            const char* oldMessage = pos->second;
                            char* newMesssage;
                            asprintf(&newMesssage, "%s, %s", oldMessage, message);
                            pageToContent[p] = newMesssage;
                            ::free((void*)oldMessage);
                        }
                    }
                };

                dyldCache->forEachImage(^(const mach_header* mh, const char* installName) {
                    dyld3::MachOAnalyzer* ma = (dyld3::MachOAnalyzer*)mh;
                    Diagnostics diag;
                    dyld3::MachOAnalyzer::LinkEditInfo leInfo;
                    ma->getLinkEditPointers(diag, leInfo);

                    if (diag.hasError())
                        return;

                    char message[1000];
                    const char* shortName = strrchr(installName, '/') + 1;

                    if ( leInfo.dyldInfo != nullptr ) {
                        // add export trie info
                        if ( leInfo.dyldInfo->export_size != 0 ) {
                            //printf("export_off=0x%X\n", leInfo.dyldInfo->export_off());
                            uint32_t exportPageOffsetStart = leInfo.dyldInfo->export_off & (-4096);
                            uint32_t exportPageOffsetEnd = (leInfo.dyldInfo->export_off + leInfo.dyldInfo->export_size) & (-4096);
                            sprintf(message, "exports from %s", shortName);
                            add_linkedit(exportPageOffsetStart, exportPageOffsetEnd, message);
                        }
                        // add binding info
                        if ( leInfo.dyldInfo->bind_size != 0 ) {
                            uint32_t bindPageOffsetStart = leInfo.dyldInfo->bind_off & (-4096);
                            uint32_t bindPageOffsetEnd = (leInfo.dyldInfo->bind_off + leInfo.dyldInfo->bind_size) & (-4096);
                            sprintf(message, "bindings from %s", shortName);
                            add_linkedit(bindPageOffsetStart, bindPageOffsetEnd, message);
                        }
                        // add lazy binding info
                        if ( leInfo.dyldInfo->lazy_bind_size != 0 ) {
                            uint32_t lazybindPageOffsetStart = leInfo.dyldInfo->lazy_bind_off & (-4096);
                            uint32_t lazybindPageOffsetEnd = (leInfo.dyldInfo->lazy_bind_off + leInfo.dyldInfo->lazy_bind_size) & (-4096);
                            sprintf(message, "lazy bindings from %s", shortName);
                            add_linkedit(lazybindPageOffsetStart, lazybindPageOffsetEnd, message);
                        }
                        // add weak binding info
                        if ( leInfo.dyldInfo->weak_bind_size != 0 ) {
                            uint32_t weakbindPageOffsetStart = leInfo.dyldInfo->weak_bind_off & (-4096);
                            uint32_t weakbindPageOffsetEnd = (leInfo.dyldInfo->weak_bind_off + leInfo.dyldInfo->weak_bind_size) & (-4096);
                            sprintf(message, "weak bindings from %s", shortName);
                            add_linkedit(weakbindPageOffsetStart, weakbindPageOffsetEnd, message);
                        }
                    } else {
                        // add export trie info
                        if ( (leInfo.exportsTrie != nullptr) && (leInfo.exportsTrie->datasize != 0) ) {
                            //printf("export_off=0x%X\n", leInfo.exportsTrie->export_off());
                            uint32_t exportPageOffsetStart = leInfo.exportsTrie->dataoff & (-4096);
                            uint32_t exportPageOffsetEnd = (leInfo.exportsTrie->dataoff + leInfo.exportsTrie->datasize) & (-4096);
                            sprintf(message, "exports from %s", shortName);
                            add_linkedit(exportPageOffsetStart, exportPageOffsetEnd, message);
                        }
                        // Chained fixups are stripped from cache binaries, so no need to check for them here
                    }
                });

                for (std::map<uint32_t, const char*>::iterator it = pageToContent.begin(); it != pageToContent.end(); ++it) {
                    printf("0x%08X %s\n", it->first, it->second);
                }
                break;
            }
            case modeSize: {
                struct TextInfo {
                    uint64_t    textSize;
                    const char* path;
                };
                __block std::vector<TextInfo> textSegments;
                dyldCache->forEachImage(^(const mach_header* mh, const char* installName) {

                    dyld3::MachOAnalyzer* ma = (dyld3::MachOAnalyzer*)mh;
                    ma->forEachSegment(^(const dyld3::MachOAnalyzer::SegmentInfo &info, bool &stop) {
                        if ( strcmp(info.segName, "__TEXT") != 0 )
                            return;
                        textSegments.push_back({ info.fileSize, installName });
                    });
                });
                std::sort(textSegments.begin(), textSegments.end(), [](const TextInfo& left, const TextInfo& right) {
                    return (left.textSize > right.textSize);
                });
                for (std::vector<TextInfo>::iterator it = textSegments.begin(); it != textSegments.end(); ++it) {
                    printf(" 0x%08llX  %s\n", it->textSize, it->path);
                }
                break;
            }
            case modePatchTable: {
                std::vector<SegmentInfo> segInfos;
                buildSegmentInfo(dyldCache, segInfos);
                __block uint32_t imageIndex = 0;
                dyldCache->forEachImage(^(const mach_header* mh, const char* installName) {
                    printf("%s:\n", installName);
                    uint64_t cacheBaseAddress = dyldCache->unslidLoadAddress();
                    uint64_t dylibBaseAddress = ((dyld3::MachOAnalyzer*)mh)->preferredLoadAddress();
                    dyldCache->forEachPatchableExport(imageIndex, ^(uint32_t dylibVMOffsetOfImpl, const char* exportName) {
                        uint64_t cacheOffsetOfImpl = (dylibBaseAddress + dylibVMOffsetOfImpl) - cacheBaseAddress;
                        printf("    export: 0x%08llX  %s\n", cacheOffsetOfImpl, exportName);
                        dyldCache->forEachPatchableUseOfExport(imageIndex, dylibVMOffsetOfImpl,
                                                               ^(uint32_t userImageIndex, uint32_t userVMOffset,
                                                                 dyld3::MachOLoaded::PointerMetaData pmd, uint64_t addend) {
                            // Get the image so that we can convert from dylib offset to cache offset
                            uint64_t mTime;
                            uint64_t inode;
                            const dyld3::MachOAnalyzer* imageMA = (dyld3::MachOAnalyzer*)(dyldCache->getIndexedImageEntry(userImageIndex, mTime, inode));
                            if ( imageMA == nullptr )
                                return;

                            SegmentInfo usageAt;
                            const uint64_t patchLocVmAddr = imageMA->preferredLoadAddress() + userVMOffset;
                            const uint64_t patchLocCacheOffset = patchLocVmAddr - cacheBaseAddress;
                            findImageAndSegment(dyldCache, segInfos, patchLocCacheOffset, &usageAt);

                            // Verify that findImage and the callback image match
                            std::string_view userInstallName = imageMA->installName();
                            assert(userInstallName == usageAt.installName);

                            if ( addend == 0 )
                                printf("        used by: %s+0x%04llX in %s\n", usageAt.segName, patchLocVmAddr-usageAt.vmAddr, usageAt.installName);
                            else
                                printf("        used by: %s+0x%04llX (addend=%lld) in %s\n", usageAt.segName, patchLocVmAddr-usageAt.vmAddr, addend, usageAt.installName);
                        });
                    });
                    ++imageIndex;
                });
                break;
            }
            case modeNone:
            case modeInfo:
            case modeSlideInfo:
            case modeVerboseSlideInfo:
            case modeTextInfo:
            case modeLocalSymbols:
            case modeJSONMap:
            case modeJSONDependents:
            case modeSectionSizes:
            case modeStrings:
            case modeObjCInfo:
            case modeObjCProtocols:
            case modeObjCImpCaches:
            case modeObjCClasses:
            case modeObjCSelectors:
            case modeSwiftProtocolConformances:
            case modeExtract:
                break;
        }
    }
    return 0;
}
