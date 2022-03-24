/*
 * Copyright (c) 2017 Apple Inc. All rights reserved.
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


#ifndef SharedCacheBuilder_h
#define SharedCacheBuilder_h

#include "CacheBuilder.h"
#include "DyldSharedCache.h"
#include "ClosureFileSystem.h"
#include "PrebuiltLoader.h"

namespace IMPCaches {
class IMPCachesBuilder;
}

class SharedCacheBuilder : public CacheBuilder {
private:
    // For Large Shared Caches, each cache is split in to chunks.  This represents a single chunk, for some number of dylibs
    struct SubCache {
        // These are the ranges in to the _sortedDylibs, for which dylibs are in this subCache.
        // For some archs, eg, armv7k, we can't put LINKEDIT in each subCache, but instead just the last one.
        // Given that, we track which of __TEXT, __DATA, __LINKEDIT for each dylib is in each subCache
        uint64_t                                    _textFirstDylibIndex            = 0;
        uint64_t                                    _textNumDylibs                  = 0;
        uint64_t                                    _dataFirstDylibIndex            = 0;
        uint64_t                                    _dataNumDylibs                  = 0;
        uint64_t                                    _linkeditFirstDylibIndex        = 0;
        uint64_t                                    _linkeditNumDylibs              = 0;

        // SubCache layouts can get quite complex with where are when to add padding, especially between caches
        // For example, for split caches, we add a small amount of LINKEDIT after DATA, then start a new subcache
        // and emit the rest of LINKEDIT.  We don't want padding between those LINKEDITs
        bool                                        _addPaddingAfterText            = true;
        bool                                        _addPaddingAfterData            = true;

        Region                                      _readExecuteRegion;
        // 0 or more __DATA regions.
        // Split caches might have 0 in some SubCache's, while regular SubCache's will have 1 or more
        std::vector<Region>                         _dataRegions;
        // Split caches might not have their own LINKEDIT
        std::optional<Region>                       _readOnlyRegion;
        UnmappedRegion                              _codeSignatureRegion;

        // Note this is mutable as the only parallel writes to it are done atomically to the bitmap
        mutable ASLR_Tracker                        _aslrTracker;
        uint64_t                                    _nonLinkEditReadOnlySize    = 0;
        uint8_t                                     _cdHashFirst[20];
        uint8_t                                     _cdHashSecond[20];

        // Rosetta.  We need to reserve space for the translation of x86_64 caches
        uint64_t                                    _rosettaReadOnlyAddr        = 0;
        uint64_t                                    _rosettaReadOnlySize        = 0;
        uint64_t                                    _rosettaReadWriteAddr       = 0;
        uint64_t                                    _rosettaReadWriteSize       = 0;

        uint64_t            dataRegionsTotalSize() const;
        uint64_t            dataRegionsSizeInUse() const;

        // Return the earliest data region by address
        const Region*       firstDataRegion() const;

        // Return the lateset data region by address
        const Region*       lastDataRegion() const;

        // Returns the highest VM Address at the end of this SubCache
        // In a regular cache this is the end of __LINKEDIT
        // It's also possible for this to be the end of __TEXT or __DATA in a split cache layout
        uint64_t highestVMAddress() const;

        // Returns the highest file offset at the end of this SubCache
        // In a regular cache this is the end of __LINKEDIT
        // It's also possible for this to be the end of __TEXT or __DATA in a split cache layout
        uint64_t highestFileOffset() const;

        const std::string   cdHashFirst() const;
        const std::string   cdHashSecond() const;
        const std::string   uuid() const;
    };
public:
    SharedCacheBuilder(const DyldSharedCache::CreateOptions& options, const dyld3::closure::FileSystem& fileSystem);

    struct CacheBuffer {
        uint8_t* bufferData = nullptr;
        uint64_t bufferSize = 0;
        std::string cdHash  = "";
        std::string uuid    = "";
    };

    void                                        build(std::vector<InputFile>& inputFiles,
                                                      std::vector<DyldSharedCache::FileAlias>& aliases);
    void                                        build(const std::vector<LoadedMachO>& dylibs,
                                                      const std::vector<LoadedMachO>& otherOsDylibsInput,
                                                      const std::vector<LoadedMachO>& osExecutables,
                                                      std::vector<DyldSharedCache::FileAlias>& aliases);
    void                                        build(const std::vector<DyldSharedCache::MappedMachO>&  dylibsToCache,
                                                      const std::vector<DyldSharedCache::MappedMachO>&  otherOsDylibs,
                                                      const std::vector<DyldSharedCache::MappedMachO>&  osExecutables,
                                                      std::vector<DyldSharedCache::FileAlias>& aliases);

    void                                        writeFile(const std::string& path);
    void                                        writeBuffers(std::vector<CacheBuffer>& cacheBuffers);
    void                                        writeSymbolFileBuffer(CacheBuffer& cacheBuffer);
    void                                        writeMapFile(const std::string& path);
    std::string                                 getMapFileBuffer() const;
    std::string                                 getMapFileJSONBuffer(const std::string& cacheDisposition) const;
    void                                        deleteBuffer();
    const std::set<std::string>                 warnings();
    const std::set<const dyld3::MachOAnalyzer*> evictions();
    const bool                                  agileSignature() const;

    void                                        forEachCacheDylib(void (^callback)(const std::string& path));
    void                                        forEachCacheSymlink(void (^callback)(const std::string& path));

    void                                        forEachDylibInfo(void (^callback)(const DylibInfo& dylib, Diagnostics& dylibDiag,
                                                                                  ASLR_Tracker& dylibASLRTracker)) override final;

    struct DylibInfo : CacheBuilder::DylibInfo {
        // <class name, metaclass> -> pointer
        std::unordered_map<IMPCaches::ClassKey, std::unique_ptr<IMPCaches::ClassData>, IMPCaches::ClassKeyHasher> impCachesClassData;

        // The ASLRTracker used to slide this dylib's __DATA* segments
        ASLR_Tracker* _aslrTracker = nullptr;
    };

private:

    void        writeSlideInfoV1();

    template <typename P> void writeSlideInfoV2(SubCache& subCache);
    template <typename P> bool makeRebaseChainV2(uint8_t* pageContent, uint16_t lastLocationOffset, uint16_t newOffset, const struct dyld_cache_slide_info2* info,
                                                 const CacheBuilder::ASLR_Tracker& aslrTracker);
    template <typename P> void addPageStartsV2(uint8_t* pageContent, const bool bitmap[], const struct dyld_cache_slide_info2* info,
                                               const CacheBuilder::ASLR_Tracker& aslrTracker,
                                               std::vector<uint16_t>& pageStarts, std::vector<uint16_t>& pageExtras);

    void        writeSlideInfoV3(SubCache& subCache);
    uint16_t    pageStartV3(uint8_t* pageContent, uint32_t pageSize, const bool bitmap[], SubCache& subCache);
    void        setPointerContentV3(dyld3::MachOLoaded::ChainedFixupPointerOnDisk* loc, uint64_t targetVMAddr, size_t next,
                                    SubCache& subCache);

    template <typename P> void writeSlideInfoV4(SubCache& subCache);
    template <typename P> bool makeRebaseChainV4(uint8_t* pageContent, uint16_t lastLocationOffset, uint16_t newOffset, const struct dyld_cache_slide_info4* info);
    template <typename P> void addPageStartsV4(uint8_t* pageContent, const bool bitmap[], const struct dyld_cache_slide_info4* info,
                                             std::vector<uint16_t>& pageStarts, std::vector<uint16_t>& pageExtras);

    struct ArchLayout
    {
        uint64_t    sharedMemoryStart;
        uint64_t    sharedMemorySize;
        uint64_t    subCacheTextLimit;
        uint64_t    sharedRegionPadding;
        uint64_t    pointerDeltaMask;
        const char* archName;
        uint16_t    csPageSize;
        uint8_t     sharedRegionAlignP2;
        uint8_t     slideInfoBytesPerPage;
        bool        sharedRegionsAreDiscontiguous;
        bool        is64;
        bool        useValueAdd;
        // True:  Split Cache layout, which is __TEXT, __TEXT, ..., __DATA, __LINKEDIT.
        // False: Regular layout, which is __TEXT, __DATA, __LINKEDIT, __TEXT, __DATA, __LINKEDIT, ...
        bool        useSplitCacheLayout;
    };

    static const ArchLayout  _s_archLayout[];
    static const char* const _s_neverStubEliminateSymbols[];

    void        makeSortedDylibs(const std::vector<LoadedMachO>& dylibs, const std::unordered_map<std::string, unsigned> sortOrder);
    void        processSelectorStrings(const std::vector<LoadedMachO>& executables, IMPCaches::HoleMap& selectorsHoleMap);
    void        parseCoalescableSegments(IMPCaches::SelectorMap& selectorMap, IMPCaches::HoleMap& selectorsHoleMap);
    void        computeSubCaches();
    void        assignSegmentAddresses(uint64_t objcROSize, uint64_t objcRWSize, uint64_t swiftROSize);
    void        assignReadExecuteSegmentAddresses(SubCache& subCache, uint64_t& addr, uint64_t& cacheFileOffset,
                                                  size_t startOffset, uint64_t objcROSize, uint64_t swiftROSize);
    void        assignObjCROAddress(SubCache& subCache, uint64_t& addr, uint64_t objcROSize);
    void        assignSwiftROAddress(SubCache& subCache, uint64_t& addr, uint64_t swiftROSize);
    void        assignDataSegmentAddresses(SubCache& subCache, uint64_t& addr, uint64_t& cacheFileOffset, uint64_t objcRWSize);
    void        assignReadOnlySegmentAddresses(SubCache& subCache, uint64_t& addr, uint64_t& cacheFileOffset);

    uint64_t    cacheOverflowAmount(const SubCache** overflowingSubCache = nullptr);
    size_t      evictLeafDylibs(uint64_t reductionTarget, std::vector<LoadedMachO>& overflowDylibs);

    void        fipsSign();
    void        codeSign(SubCache& subCache);
    uint64_t    pathHash(const char* path);
    static void writeSharedCacheHeader(const SubCache& subCache, const DyldSharedCache::CreateOptions& options,
                                       const ArchLayout& layout,
                                       uint32_t osVersion, uint32_t altPlatform, uint32_t altOsVersion,
                                       uint64_t cacheType);
    void        writeCacheHeader();
    void        findDylibAndSegment(const void* contentPtr, std::string& dylibName, std::string& segName);
    void        buildDylibJITLoaders(dyld4::RuntimeState& state, const std::vector<DyldSharedCache::FileAlias>& aliases, std::vector<class dyld4::JustInTimeLoader*>& jitLoaders);
    void        buildDylibsPrebuiltLoaderSet(const MachOAnalyzer* aMain, const std::vector<DyldSharedCache::FileAlias>& aliases);
    void        bindDylibs(const MachOAnalyzer*, const std::vector<DyldSharedCache::FileAlias>& aliases);
    void        buildLaunchSets(const std::vector<LoadedMachO>& osExecutables, const std::vector<LoadedMachO>& otherDylibs, const std::vector<LoadedMachO>& moreOtherDylibs);
    void        markPaddingInaccessible();
    void        buildPatchTables(const std::unordered_map<std::string, uint32_t>& loaderToIndexMap);
    void        buildDylibsTrie(const std::vector<DyldSharedCache::FileAlias>& aliases, std::unordered_map<std::string, uint32_t>& dylibPathToDylibIndex);

    bool        writeSubCache(const SubCache& subCache, void (^cacheSizeCallback)(uint64_t size), bool (^copyCallback)(const uint8_t* src, uint64_t size, uint64_t dstOffset));

    // implemented in OptimizerObjC.cpp
    void        optimizeObjC(bool impCachesSuccess, const std::vector<const IMPCaches::Selector*> & inlinedSelectors);
    uint32_t    computeReadOnlyObjC(uint32_t selRefCount, uint32_t classDefCount, uint32_t protocolDefCount);
    uint32_t    computeReadWriteObjC(uint32_t imageCount, uint32_t protocolDefCount);

    // implemented in OptimizerSwift.cpp
    void        optimizeSwift();
    uint32_t    computeReadOnlySwift();

    void        emitContantObjects();

    void        writeSubCacheFile(const SubCache& subCache, const std::string& path);

    Region&     getSharedCacheReadOnlyRegion();

    typedef std::unordered_map<std::string, const dyld3::MachOAnalyzer*> InstallNameToMA;

    typedef uint64_t                                                CacheOffset;

    std::vector<DylibInfo>                      _sortedDylibs;
    std::vector<SubCache>                       _subCaches;
    // Some metadata should only be added to a single SubCache.  This tracks which one
    SubCache*                                   _objcReadOnlyMetadataSubCache           = nullptr;
    SubCache*                                   _objcReadWriteMetadataSubCache          = nullptr;
    SubCache                                    _localSymbolsSubCache;
    std::vector<uint8_t>                        _localSymbolsSubCacheBuffer;
    std::set<const dyld3::MachOAnalyzer*>       _evictions;
    const ArchLayout*                           _archLayout                             = nullptr;
    uint32_t                                    _aliasCount                             = 0;
    uint8_t*                                    _objcReadOnlyBuffer                     = nullptr;
    uint64_t                                    _objcReadOnlyBufferSizeUsed             = 0;
    uint64_t                                    _objcReadOnlyBufferSizeAllocated        = 0;
    uint8_t*                                    _objcReadWriteBuffer                    = nullptr;
    uint64_t                                    _objcReadWriteBufferSizeAllocated       = 0;
    uint64_t                                    _objcReadWriteFileOffset                = 0;
    uint64_t                                    _selectorStringsFromExecutables         = 0;
    uint8_t*                                    _swiftReadOnlyBuffer                    = nullptr;
    uint64_t                                    _swiftReadOnlyBufferSizeAllocated       = 0;
    InstallNameToMA                             _installNameToCacheDylib;
    std::unordered_map<std::string, uint32_t>   _dataDirtySegsOrder;
    std::map<void*, std::string>                _missingWeakImports;
    const dyld4::PrebuiltLoaderSet*             _cachedDylibsLoaderSet                  = nullptr;
    bool                                        _someDylibsUsedChainedFixups            = false;
    std::unordered_set<std::string>             _dylibAliases;
    IMPCaches::IMPCachesBuilder* _impCachesBuilder;

    // Cache patching

    // This contains all clients of a given dylib, ie, all dylibs which bound to a given dylib.
    // It also tracks all the locations of all the binds for patching later
    struct DylibSymbolClients
    {
        struct dyld_cache_patchable_location
        {
            dyld_cache_patchable_location(uint64_t cacheOff, MachOLoaded::PointerMetaData pmd, uint64_t addend);

            // For std::set, to remove duplicate entries
            bool operator==(const dyld_cache_patchable_location& other) const {
                return this->cacheOffset == other.cacheOffset;
            }

            uint64_t            cacheOffset;
            uint64_t            high7                   : 7,
                                addend                  : 5,    // 0..31
                                authenticated           : 1,
                                usesAddressDiversity    : 1,
                                key                     : 2,
                                discriminator           : 16;
        };

        // Records all uses of a given locatoin
        struct Uses
        {
            std::map<CacheOffset, std::vector<dyld_cache_patchable_location>> _uses;
        };

        // Map from client dylib to the locations in which it uses a given symbol
        // Map from client dylib to the cache offsets it binds to
        std::map<const dyld3::MachOLoaded*, Uses> _clientToUses;

        // Set of all exports from the exporting dylib that are eligible for patching
        std::set<CacheOffset>                     _usedExports;
    };

    // Map from each dylib to the dylibs which were bound to it
    std::unordered_map<const dyld3::MachOLoaded*, DylibSymbolClients>           _dylibToItsClients;
    // Set of weak def/ref locations for each dylib
    std::set<std::pair<const dyld3::MachOLoaded*, CacheOffset>>                 _dylibWeakExports;
    std::unordered_map<CacheOffset, std::string>                                _exportsToName;
};



#endif /* SharedCacheBuilder_h */
