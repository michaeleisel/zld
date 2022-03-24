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


#ifndef DyldSharedCache_h
#define DyldSharedCache_h

#include <TargetConditionals.h>
#include <uuid/uuid.h>

#if (BUILDING_LIBDYLD || BUILDING_DYLD)
#include <sys/types.h>
#endif

#if !(BUILDING_LIBDYLD || BUILDING_DYLD)
#include <set>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#endif

#include "dyld_cache_format.h"
#include "CachePatching.h"
#include "Diagnostics.h"
#include "MachOAnalyzer.h"
#include "JSON.h"

namespace dyld4 {
    class PrebuiltLoader;
    struct PrebuiltLoaderSet;
}

namespace objc_opt {
struct objc_opt_t;
}

struct SwiftOptimizationHeader;

class VIS_HIDDEN DyldSharedCache
{
public:

#if BUILDING_CACHE_BUILDER
    enum CodeSigningDigestMode
    {
        SHA256only = 0,
        SHA1only   = 1,
        Agile      = 2
    };

    enum class LocalSymbolsMode {
        keep,
        unmap,
        strip
    };

    struct CreateOptions
    {
        std::string                                 outputFilePath;
        std::string                                 outputMapFilePath;
        const dyld3::GradedArchs*                   archs;
        dyld3::Platform                             platform;
        LocalSymbolsMode                            localSymbolMode;
        bool                                        optimizeStubs;
        bool                                        optimizeDyldDlopens;
        bool                                        optimizeDyldLaunches;
        CodeSigningDigestMode                       codeSigningDigestMode;
        bool                                        dylibsRemovedDuringMastering;
        bool                                        inodesAreSameAsRuntime;
        bool                                        cacheSupportsASLR;
        bool                                        forSimulator;
        bool                                        isLocallyBuiltCache;
        bool                                        verbose;
        bool                                        evictLeafDylibsOnOverflow;
        std::unordered_map<std::string, unsigned>   dylibOrdering;
        std::unordered_map<std::string, unsigned>   dirtyDataSegmentOrdering;
        dyld3::json::Node                           objcOptimizations;
        std::string                                 loggingPrefix;
        // Customer and dev caches share a local symbols file.  Only one will get this set to emit the file
        std::string                                 localSymbolsPath;
    };

    struct MappedMachO
    {
                                    MappedMachO()
                                            : mh(nullptr), length(0), isSetUID(false), protectedBySIP(false), sliceFileOffset(0), modTime(0), inode(0) { }
                                    MappedMachO(const std::string& path, const dyld3::MachOAnalyzer* p, size_t l, bool isu, bool sip, uint64_t o, uint64_t m, uint64_t i)
                                            : runtimePath(path), mh(p), length(l), isSetUID(isu), protectedBySIP(sip), sliceFileOffset(o), modTime(m), inode(i) { }

        std::string                 runtimePath;
        const dyld3::MachOAnalyzer* mh;
        size_t                      length;
        uint64_t                    isSetUID        :  1,
                                    protectedBySIP  :  1,
                                    sliceFileOffset : 62;
        uint64_t                    modTime;                // only recorded if inodesAreSameAsRuntime
        uint64_t                    inode;                  // only recorded if inodesAreSameAsRuntime
    };

    struct CreateResults
    {
        std::string                             errorMessage;
        std::set<std::string>                   warnings;
        std::set<const dyld3::MachOAnalyzer*>   evictions;
    };


    struct FileAlias
    {
        std::string             realPath;
        std::string             aliasPath;
    };


    // This function verifies the set of dylibs that will go into the cache are self contained.  That the depend on no dylibs
    // outset the set.  It will call back the loader function to try to find any mising dylibs.
    static bool verifySelfContained(std::vector<MappedMachO>& dylibsToCache,
                                    std::unordered_set<std::string>& badZippered,
                                    MappedMachO (^loader)(const std::string& runtimePath, Diagnostics& diag), std::vector<std::pair<DyldSharedCache::MappedMachO, std::set<std::string>>>& excluded);


    //
    // This function is single threaded and creates a shared cache. The cache file is created in-memory.
    //
    // Inputs:
    //      options:        various per-platform flags
    //      dylibsToCache:  a list of dylibs to include in the cache
    //      otherOsDylibs:  a list of other OS dylibs and bundle which should have load info added to the cache
    //      osExecutables:  a list of main executables which should have closures created in the cache
    //
    // Returns:
    //    On success:
    //         cacheContent: start of the allocated cache buffer which must be vm_deallocated after the caller writes out the buffer.
    //         cacheLength:  size of the allocated cache buffer
    //         cdHash:       hash of the code directory of the code blob of the created cache
    //         warnings:     all warning messsages generated during the creation of the cache
    //
    //    On failure:
    //         cacheContent: nullptr
    //         errorMessage: the string describing why the cache could not be created
    //         warnings:     all warning messsages generated before the failure
    //
    static CreateResults create(const CreateOptions&               options,
                                const dyld3::closure::FileSystem&  fileSystem,
                                const std::vector<MappedMachO>&    dylibsToCache,
                                const std::vector<MappedMachO>&    otherOsDylibs,
                                const std::vector<MappedMachO>&    osExecutables);


    //
    // Returns a text "map" file as a big string
    //
    std::string         mapFile() const;

#endif // BUILDING_CACHE_BUILDER


    //
    // Returns the architecture name of the shared cache, e.g. "arm64"
    //
    const char*         archName() const;


    //
    // Returns the platform the cache is for
    //
    dyld3::Platform    platform() const;


    //
    // Iterates over each dylib in the cache
    //
    void                forEachImage(void (^handler)(const mach_header* mh, const char* installName)) const;
    void                forEachDylib(void (^handler)(const dyld3::MachOAnalyzer* ma, const char* installName, uint32_t imageIndex, uint64_t inode, uint64_t mtime, bool& stop)) const;


    //
    // Searches cache for dylib with specified path
    //
    bool                hasImagePath(const char* dylibPath, uint32_t& imageIndex) const;


    //
    // Is this path (which we know is in the shared cache), overridable
    //
    bool                isOverridablePath(const char* dylibPath) const;


    //
    // Path is to a dylib in the cache and this is an optimized cache so that path cannot be overridden
    //
    bool                hasNonOverridablePath(const char* dylibPath) const;


    //
    // Check if this shared cache file contains local symbols info
    // Note this might be the .symbols file, in which case this returns true
    // The main cache file in a split cache will return false here.
    // Use hasLocalSymbolsInfoFile() instead to see if a main cache has a .symbols file
    //
    const bool          hasLocalSymbolsInfo() const;


    //
    // Check if this cache file has a reference to a local symbols file
    //
    const bool          hasLocalSymbolsInfoFile() const;


    //
    // Get code signature mapped address
    //
    uint64_t             getCodeSignAddress() const;


    //
    // Searches cache for dylib with specified mach_header
    //
    bool                findMachHeaderImageIndex(const mach_header* mh, uint32_t& imageIndex) const;

   //
    // Iterates over each dylib in the cache
    //
    void                forEachImageEntry(void (^handler)(const char* path, uint64_t mTime, uint64_t inode)) const;


    //
    // Get image entry from index
    //
    const mach_header*  getIndexedImageEntry(uint32_t index, uint64_t& mTime, uint64_t& node) const;


    // iterates over all dylibs and aliases
    void                forEachDylibPath(void (^handler)(const char* dylibPath, uint32_t index)) const;

    //
    // If path is a dylib in the cache, return is mach_header
    //
    const dyld3::MachOFile* getImageFromPath(const char* dylibPath) const;

    //
    // Get image path from index
    //
    const char*         getIndexedImagePath(uint32_t index) const;

    //
    // Get the canonical (dylib) path for a given path, which may be a symlink to something in the cache
    //
    const char*         getCanonicalPath(const char* path) const;

    //
    // Iterates over each text segment in the cache
    //
    void                forEachImageTextSegment(void (^handler)(uint64_t loadAddressUnslid, uint64_t textSegmentSize, const uuid_t dylibUUID, const char* installName, bool& stop)) const;


    //
    // Iterates over each of the three regions in the cache
    //
    void                forEachRegion(void (^handler)(const void* content, uint64_t vmAddr, uint64_t size,
                                                      uint32_t initProt, uint32_t maxProt, uint64_t flags,
                                                      bool& stopRegion)) const;


    //
    // Iterates over each of the mappings in the cache and all subCaches
    // After iterating over all mappings, calls the subCache handler if its not-null
    //
    void                forEachRange(void (^mappingHandler)(const char* mappingName,
                                                            uint64_t unslidVMAddr, uint64_t vmSize,
                                                            uint32_t cacheFileIndex, uint64_t fileOffset,
                                                            uint32_t initProt, uint32_t maxProt,
                                                            bool& stopRange),
                                     void (^subCacheHandler)(const DyldSharedCache* subCache, uint32_t cacheFileIndex) = nullptr) const;

    //
    // Iterates over each of the subCaches, including the current cache
    //
    void                forEachCache(void (^handler)(const DyldSharedCache* cache, bool& stopCache)) const;

    //
    // Returns the number of subCache files
    //
    uint32_t            numSubCaches() const;

    //
    // Returns the address of the the first dyld_cache_image_info in the cache
    //
    const dyld_cache_image_info* images() const;
    
    //
    // Returns the number of images in the cache
    //
    uint32_t imagesCount() const;

    //
    // Get local symbols nlist entries
    //
    static const void*  getLocalNlistEntries(const dyld_cache_local_symbols_info* localInfo);
    const void*         getLocalNlistEntries() const;


    //
    // Get local symbols nlist count
    //
    const uint32_t      getLocalNlistCount() const;


    //
    // Get local symbols strings
    //
    static const char*  getLocalStrings(const dyld_cache_local_symbols_info* localInfo);
    const char*         getLocalStrings() const;


    //
    // Get local symbols strings size
    //
    const uint32_t       getLocalStringsSize() const;


     //
     // Iterates over each local symbol entry in the cache
     //
     void                forEachLocalSymbolEntry(void (^handler)(uint64_t dylibCacheVMOffset, uint32_t nlistStartIndex, uint32_t nlistCount, bool& stop)) const;

    //
    // Returns if an address range is in this cache, and if so if in a read-only area
    //
    bool                inCache(const void* addr, size_t length, bool& readOnly) const;

    //
    // Returns true if a path is an alternate path (symlink)
    //
    bool                isAlias(const char* path) const;

    //
    // returns address the cache would load at if unslid
    //
    uint64_t            unslidLoadAddress() const;


    //
    // returns UUID of cache
    //
    void                getUUID(uuid_t uuid) const;


    //
    // returns the vm size required to map cache
    //
    uint64_t            mappedSize() const;


    //
    // searches cache for PrebuiltLoader for image
    //
    const dyld4::PrebuiltLoader* findPrebuiltLoader(const char* path) const;


    //
    // calculate how much cache was slid when loaded
    //
    intptr_t    slide() const;

   //
    // iterates all pre-built closures for program
    //
    void forEachLaunchLoaderSet(void (^handler)(const char* executableRuntimePath, const dyld4::PrebuiltLoaderSet* pbls)) const;

    //
    // searches cache for PrebuiltLoader for program
    //
    const dyld4::PrebuiltLoaderSet* findLaunchLoaderSet(const char* executablePath) const;

    //
    // searches cache for PrebuiltLoader for program by cdHash
    //
    bool hasLaunchLoaderSetWithCDHash(const char* cdHashString) const;

    //
    // Returns the pointer to the slide info for this cache
    //
    const dyld_cache_slide_info* legacyCacheSlideInfo() const;

    //
    // Returns a pointer to the __DATA region mapping in the cache
    //
    const dyld_cache_mapping_info* legacyCacheDataRegionMapping() const;

    //
    // Returns a pointer to the start of the __DATA region in the cache
    //
    const uint8_t* legacyCacheDataRegionBuffer() const;

    //
    // Returns a pointer to the shared cache optimized Objective-C data structures
    //
    const objc_opt::objc_opt_t* objcOpt() const;

    //
    // Returns a pointer to the shared cache optimized Objective-C pointer structures
    //
    const void* objcOptPtrs() const;

#if !(BUILDING_LIBDYLD || BUILDING_DYLD)
    //
    // In Large Shared Caches, shared cache relative method lists are offsets from the magic
    // selector in libobjc.
    // Returns the VM address of that selector, if it exists
    //
    uint64_t sharedCacheRelativeSelectorBaseVMAddress() const;
#endif

    //
    // Returns a pointer to the shared cache optimized Swift data structures
    //
    const SwiftOptimizationHeader* swiftOpt() const;

    // Returns true if the cache has any slide info, either old style on a single data region
    // or on each individual data mapping
    bool                hasSlideInfo() const;

    void                forEachSlideInfo(void (^handler)(uint64_t mappingStartAddress, uint64_t mappingSize,
                                                         const uint8_t* mappingPagesStart,
                                                         uint64_t slideInfoOffset, uint64_t slideInfoSize,
                                                         const dyld_cache_slide_info* slideInfoHeader)) const;


    //
    // returns true if the offset is in the TEXT of some cached dylib and sets *index to the dylib index
    //
    bool              addressInText(uint64_t cacheOffset, uint32_t* index) const;

    uint32_t          patchInfoVersion() const;
    uint32_t          patchableExportCount(uint32_t imageIndex) const;
    void              forEachPatchableExport(uint32_t imageIndex, void (^handler)(uint32_t dylibVMOffsetOfImpl, const char* exportName)) const;
    void              forEachPatchableUseOfExport(uint32_t imageIndex, uint32_t dylibVMOffsetOfImpl,
                                                  void (^handler)(uint32_t userImageIndex, uint32_t userVMOffset,
                                                                  dyld3::MachOLoaded::PointerMetaData pmd, uint64_t addend)) const;
    // Use this when you have a root of at imageIndex, and are trying to patch a cached dylib at userImageIndex
    bool              shouldPatchClientOfImage(uint32_t imageIndex, uint32_t userImageIndex) const;
    void              forEachPatchableUseOfExportInImage(uint32_t imageIndex, uint32_t dylibVMOffsetOfImpl, uint32_t userImageIndex,
                                                         void (^handler)(uint32_t userVMOffset, dyld3::MachOLoaded::PointerMetaData pmd, uint64_t addend)) const;
    // Note, use this for weak-defs when you just want all uses of an export, regardless of which dylib they are in.
    void              forEachPatchableUseOfExport(uint32_t imageIndex, uint32_t dylibVMOffsetOfImpl,
                                                  void (^handler)(uint64_t cacheVMOffset,
                                                                  dyld3::MachOLoaded::PointerMetaData pmd, uint64_t addend)) const;

#if !(BUILDING_LIBDYLD || BUILDING_DYLD)
    // MRM map file generator
    std::string generateJSONMap(const char* disposition) const;

    // This generates a JSON representation of deep reverse dependency information in the cache.
    // For each dylib, the output will contain the list of all the other dylibs transitively
    // dependening on that library. (For example, the entry for libsystem will contain almost
    // all of the dylibs in the cache ; a very high-level framework such as ARKit will have way
    // fewer dependents).
    // This is used by the shared cache ordering script to put "deep" dylibs used by everybody
    // closer to the center of the cache.
    std::string generateJSONDependents() const;
#endif

    // Note these enum entries are only valid for 64-bit archs.
    enum class ConstantClasses {
        cfStringAtomSize = 32
    };

    // Returns the start and size of the range in the shared cache of the ObjC constants, such as
    // all of the CFString's which have been moved in to a contiguous range
    std::pair<const void*, uint64_t> getObjCConstantRange() const;

#if !(BUILDING_LIBDYLD || BUILDING_DYLD)
    dyld3::MachOAnalyzer::VMAddrConverter makeVMAddrConverter(bool contentRebased) const;
#endif

    // Returns if the the given MachO is in the shared cache range.
    // Returns false if the cache is null.
    static bool inDyldCache(const DyldSharedCache* cache, const dyld3::MachOFile* mf);

#if !(BUILDING_LIBDYLD || BUILDING_DYLD)
    //
    // Apply rebases for manually mapped shared cache
    //
    void applyCacheRebases() const;

    // mmap() an shared cache file read/only but laid out like it would be at runtime
    static const DyldSharedCache* mapCacheFile(const char* path,
                                               uint64_t baseCacheUnslidAddress,
                                               uint8_t* buffer);

    static std::vector<const DyldSharedCache*> mapCacheFiles(const char* path);
#endif

    dyld_cache_header header;

    // The most mappings we could generate.
    // For now its __TEXT, __DATA_CONST, __DATA_DIRTY, __DATA, __LINKEDIT,
    // and optionally also __AUTH, __AUTH_CONST, __AUTH_DIRTY
    static const uint32_t MaxMappings = 8;

private:
    // Returns a variable of type "const T" which corresponds to the header field with the given unslid address
    template<typename T>
    const T getAddrField(uint64_t addr) const;

#if !(BUILDING_LIBDYLD || BUILDING_DYLD)
    void fillMachOAnalyzersMap(std::unordered_map<std::string,dyld3::MachOAnalyzer*> & dylibAnalyzers) const;
    void computeReverseDependencyMapForDylib(std::unordered_map<std::string, std::set<std::string>> &reverseDependencyMap, const std::unordered_map<std::string,dyld3::MachOAnalyzer*> & dylibAnalyzers, const std::string &loadPath) const;
    void computeReverseDependencyMap(std::unordered_map<std::string, std::set<std::string>> &reverseDependencyMap) const;
    void findDependentsRecursively(std::unordered_map<std::string, std::set<std::string>> &transitiveDependents, const std::unordered_map<std::string, std::set<std::string>> &reverseDependencyMap, std::set<std::string> & visited, const std::string &loadPath) const;
    void computeTransitiveDependents(std::unordered_map<std::string, std::set<std::string>> & transitiveDependents) const;
#endif
};








#endif /* DyldSharedCache_h */
