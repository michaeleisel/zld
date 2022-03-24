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

#ifndef PreBuiltObjC_h
#define PreBuiltObjC_h

#include "Array.h"
#include "Map.h"
#include "DyldSharedCache.h"
#include "PrebuiltLoader.h"
#include <optional>

struct SelectorFixup;

namespace objc {
class SelectorHashTable;
class ClassHashTable;
class ProtocolHashTable;
struct PerfectHash;
}

namespace dyld4 {

class BumpAllocator;


struct HashPointer {
    template<typename T>
    static size_t hash(const T* v) {
        return std::hash<const T*>{}(v);
    }
};

struct EqualPointer {
    template<typename T>
    static bool equal(const T* s1, const T* s2) {
        return s1 == s2;
    }
};

struct HashUInt64 {
    static size_t hash(const uint64_t& v) {
        return std::hash<uint64_t>{}(v);
    }
};

struct EqualUInt64 {
    static bool equal(uint64_t s1, uint64_t s2) {
        return s1 == s2;
    }
};

struct HashUInt16 {
    static size_t hash(const uint16_t& v) {
        return std::hash<uint16_t>{}(v);
    }
};

struct EqualUInt16 {
    static bool equal(uint16_t s1, uint16_t s2) {
        return s1 == s2;
    }
};

// Precomputed perfect hash table of strings.
// Base class for PrebuiltLoader precomputed selector table and class table.
class VIS_HIDDEN ObjCStringTable {
    // A string table is ultimately an array of BindTargets, each of which
    // is either a sentintel or a reference to a string in some binary
    // The table itself is a power-of-2 sized array, where each string is a perfect hash.
    // In addition to the array of targets, we also have arrays of scrambles and tabs used
    // to drive the perfect hash.
protected:

    uint32_t capacity;
    uint32_t occupied;
    uint32_t shift;
    uint32_t mask;
    uint32_t roundedTabSize;
    uint32_t roundedCheckBytesSize;
    uint64_t salt;

    uint32_t scramble[256];
    uint8_t tab[0];                     /* tab[mask+1] (always power-of-2). Rounded up to roundedTabSize */
    // uint8_t checkbytes[capacity];    /* check byte for each string. Rounded up to roundedCheckBytesSize */
    // BindTargetRef offsets[capacity]; /* offsets from &capacity to cstrings */

    uint8_t* checkBytesOffset() {
        return &tab[roundedTabSize];
    }

    const uint8_t* checkBytesOffset() const {
        return &tab[roundedTabSize];
    }

    PrebuiltLoader::BindTargetRef* targetsOffset() {
        return (PrebuiltLoader::BindTargetRef*)(checkBytesOffset() + roundedCheckBytesSize);
    }

    const PrebuiltLoader::BindTargetRef* targetsOffset() const {
        return (PrebuiltLoader::BindTargetRef*)(checkBytesOffset() + roundedCheckBytesSize);
    }

    dyld3::Array<uint8_t> checkBytes() {
        return dyld3::Array<uint8_t>((uint8_t*)checkBytesOffset(), capacity, capacity);
    }
    const dyld3::Array<uint8_t> checkBytes() const {
        return dyld3::Array<uint8_t>((uint8_t*)checkBytesOffset(), capacity, capacity);
    }

    dyld3::Array<PrebuiltLoader::BindTargetRef> targets() {
        return dyld3::Array<PrebuiltLoader::BindTargetRef>((PrebuiltLoader::BindTargetRef*)targetsOffset(), capacity, capacity);
    }
    const dyld3::Array<PrebuiltLoader::BindTargetRef> targets() const {
        return dyld3::Array<PrebuiltLoader::BindTargetRef>((PrebuiltLoader::BindTargetRef*)targetsOffset(), capacity, capacity);
    }

    uint32_t hash(const char *key, size_t keylen) const;

    uint32_t hash(const char *key) const
    {
        return hash(key, strlen(key));
    }

    // The check bytes areused to reject strings that aren't in the table
    // without paging in the table's cstring data. This checkbyte calculation
    // catches 4785/4815 rejects when launching Safari; a perfect checkbyte
    // would catch 4796/4815.
    uint8_t checkbyte(const char *key, size_t keylen) const
    {
        return ((key[0] & 0x7) << 5) | ((uint8_t)keylen & 0x1f);
    }

    uint8_t checkbyte(const char *key) const
    {
        return checkbyte(key, strlen(key));
    }

    static inline uint64_t align(uint64_t addr, uint8_t p2)
    {
        uint64_t mask = (1 << p2);
        return (addr + mask - 1) & (-mask);
    }

    static PrebuiltLoader::BindTargetRef getSentinel() {
        return PrebuiltLoader::BindTargetRef::makeAbsolute(0);
    }

public:

    enum : uint32_t {
        indexNotFound = ~0U
    };

    uint32_t getIndex(const char *key) const
    {
        size_t keylen = strlen(key);
        uint32_t h = hash(key, keylen);

        // Use check byte to reject without paging in the table's cstrings
        uint8_t h_check = checkBytes()[h];
        uint8_t key_check = checkbyte(key, keylen);
        if (h_check != key_check)
            return indexNotFound;
        return h;
    }

    std::optional<PrebuiltLoader::BindTargetRef> getPotentialTarget(const char *key) const
    {
        uint32_t index = getIndex(key);
        if (index == indexNotFound)
            return {};
        return targets()[index];
    }

    static size_t size(const objc::PerfectHash& phash);

    // Get a string if it has an entry in the table
    const char* getString(const char* selName, RuntimeState&) const;

    void write(const objc::PerfectHash& phash, const Array<std::pair<const char*, PrebuiltLoader::BindTarget>>& strings);
};

class VIS_HIDDEN ObjCSelectorOpt : public ObjCStringTable {
public:
    // Get a string if it has an entry in the table
    // Returns true if an entry is found and sets the loader ref and vmOffset.
    const char* getStringAtIndex(uint32_t index, RuntimeState&) const;

    void forEachString(void (^callback)(const PrebuiltLoader::BindTargetRef& target)) const;
};

class VIS_HIDDEN ObjCClassOpt : public ObjCStringTable {
    // This table starts off with the string hash map.  If we find the class name string at a
    // given index, then we can find the associated class information at the same index in the
    // classOffsets table.

    // If classOffsets[i] points to a regular bind target, then that is an offset in to an image
    // for the class in question.
    // If classOffsets[i] points to an abolute symbol then that is an index in to the duplicates table here
    // which is a list of implementations for that class.
private:
    // ...ObjCStringTable fields...
    // PrebuiltLoader::BindTargetRef classTargets[capacity]; /* offsets from &capacity to class_t and header_info */
    // uint64_t duplicateCount;
    // PrebuiltLoader::BindTargetRef duplicateTargets[duplicatedClasses];

    PrebuiltLoader::BindTargetRef* classTargetsStart() { return (PrebuiltLoader::BindTargetRef*)&targetsOffset()[capacity]; }
    const PrebuiltLoader::BindTargetRef* classTargetsStart() const { return (const PrebuiltLoader::BindTargetRef*)&targetsOffset()[capacity]; }

    dyld3::Array<PrebuiltLoader::BindTargetRef> classTargets() {
        return dyld3::Array<PrebuiltLoader::BindTargetRef>((PrebuiltLoader::BindTargetRef*)classTargetsStart(), capacity, capacity);
    }
    const dyld3::Array<PrebuiltLoader::BindTargetRef> classTargets() const {
        return dyld3::Array<PrebuiltLoader::BindTargetRef>((PrebuiltLoader::BindTargetRef*)classTargetsStart(), capacity, capacity);
    }

    uint64_t& duplicateCount() { return *(uint64_t*)&classTargetsStart()[capacity]; }
    const uint64_t& duplicateCount() const { return *(const uint64_t*)&classTargetsStart()[capacity]; }

    PrebuiltLoader::BindTargetRef* duplicateOffsetsStart() { return (PrebuiltLoader::BindTargetRef*)(&duplicateCount()+1); }
    const PrebuiltLoader::BindTargetRef* duplicateOffsetsStart() const { return (const PrebuiltLoader::BindTargetRef*)(&duplicateCount()+1); }

    dyld3::Array<PrebuiltLoader::BindTargetRef> duplicateTargets() {
        uintptr_t count = (uintptr_t)duplicateCount();
        return dyld3::Array<PrebuiltLoader::BindTargetRef>((PrebuiltLoader::BindTargetRef*)duplicateOffsetsStart(), count, count);
    }
    const dyld3::Array<PrebuiltLoader::BindTargetRef> duplicateTargets() const {
        uintptr_t count = (uintptr_t)duplicateCount();
        return dyld3::Array<PrebuiltLoader::BindTargetRef>((PrebuiltLoader::BindTargetRef*)duplicateOffsetsStart(), count, count);
    }

public:

    static size_t size(const objc::PerfectHash& phash, uint32_t numClassesWithDuplicates,
                       uint32_t totalDuplicates);

    bool forEachClass(const char* className, RuntimeState& state,
                      void (^callback)(void* classPtr, bool isLoaded, bool* stop)) const;

    void forEachClass(RuntimeState& state,
                      void (^callback)(const PrebuiltLoader::BindTargetRef& nameTarget,
                                       const Array<PrebuiltLoader::BindTargetRef>& implTargets)) const;

    bool hasDuplicates() const { return duplicateCount() != 0; }

    void write(const objc::PerfectHash& phash, const Array<std::pair<const char*, PrebuiltLoader::BindTarget>>& strings,
               const dyld3::CStringMultiMapTo<PrebuiltLoader::BindTarget>& classes,
               uint32_t numClassesWithDuplicates, uint32_t totalDuplicates);
};

//
// PrebuiltObjC computes read-only optimized data structures to store in the PrebuiltLoaderSet
//
struct PrebuiltObjC {

    typedef dyld3::CStringMapTo<Loader::BindTarget>                               SelectorMapTy;
    typedef std::pair<const dyld3::MachOAnalyzer*, const Loader*>                 SharedCacheLoadedImage;
    typedef dyld3::Map<uint16_t, SharedCacheLoadedImage, HashUInt16, EqualUInt16> SharedCacheImagesMapTy;
    typedef dyld3::CStringMapTo<Loader::BindTarget>                               DuplicateClassesMapTy;

    struct ObjCOptimizerImage {

        ObjCOptimizerImage(const JustInTimeLoader* jitLoader, uint64_t loadAddress, uint32_t pointerSize);

        ~ObjCOptimizerImage() {
        }

        void visitReferenceToObjCSelector(const objc::SelectorHashTable* objcSelOpt,
                                          const SelectorMapTy& appSelectorMap,
                                          uint64_t selectorStringVMAddr, uint64_t selectorReferenceVMAddr,
                                          const char* selectorString);

        void visitClass(const void* dyldCacheBase,
                        const objc::ClassHashTable* objcClassOpt,
                        const SharedCacheImagesMapTy& sharedCacheImagesMap,
                        const DuplicateClassesMapTy& duplicateSharedCacheClasses,
                        uint64_t classVMAddr, uint64_t classNameVMAddr, const char* className);

        void visitProtocol(const objc::ProtocolHashTable* objcProtocolOpt,
                           const SharedCacheImagesMapTy& sharedCacheImagesMap,
                           uint64_t protocolVMAddr, uint64_t protocolNameVMAddr, const char* protocolName);

#if BUILDING_CACHE_BUILDER || BUILDING_CLOSURE_UTIL
        void calculateMissingWeakImports(RuntimeState& state);
#endif

        // Returns true if the given vm address is a pointer to null
        bool isNull(uint64_t vmAddr, const dyld3::MachOAnalyzer* ma, intptr_t slide) const;

        // On object here is either a class or protocol, which both look the same to our optimisation
        struct ObjCObject {
            const char* name;
            uint64_t    nameRuntimeOffset;
            uint64_t    valueRuntimeOffset;
        };

        const JustInTimeLoader*         jitLoader               = nullptr;
        uint32_t                        pointerSize             = 0;
        uint64_t                        loadAddress;
        Diagnostics                     diag;

        // Class and protocol optimisation data structures
        dyld3::OverflowSafeArray<ObjCObject>                                    classLocations;
        dyld3::OverflowSafeArray<ObjCObject>                                    protocolLocations;
        dyld3::OverflowSafeArray<bool>                                          protocolISAFixups;
        DuplicateClassesMapTy                                                   duplicateSharedCacheClassMap;
        dyld3::Map<uint64_t, uint32_t, HashUInt64, EqualUInt64>                 protocolIndexMap;

#if BUILDING_CACHE_BUILDER || BUILDING_CLOSURE_UTIL
        dyld3::Map<uint64_t, bool, HashUInt64, EqualUInt64>                     missingWeakImportOffsets;
#endif

        // Selector optimsation data structures
        dyld3::OverflowSafeArray<PrebuiltLoader::BindTargetRef>                 selectorFixups;
        SelectorMapTy                                                           selectorMap;

        ObjCBinaryInfo                                                          binaryInfo;
    };

    PrebuiltObjC() = default;
    ~PrebuiltObjC();

    void make(Diagnostics& diag, RuntimeState& state);

    // Adds the results from this image to the tables for the whole app
    void commitImage(const ObjCOptimizerImage& image);

    // Generates the final hash tables given all previously analysed images
    void generateHashTables(RuntimeState& state);

    // Generates the fixups for each individual image
    void generatePerImageFixups(RuntimeState& state, uint32_t pointerSize);

    // Serializes the per-image objc fixups for the given loader.
    // Returns 0 if no per-image fixups exist.  Otherwise returns their offset
    uint32_t serializeFixups(const Loader& jitLoader, BumpAllocator& allocator) const;

    static void forEachSelectorReferenceToUnique(RuntimeState& state,
                                                 const dyld3::MachOAnalyzer* ma,
                                                 uint64_t loadAddress,
                                                 const ObjCBinaryInfo& binaryInfo,
                                                 const dyld3::MachOAnalyzer::VMAddrConverter& vmAddrConverter,
                                                 void (^callback)(uint64_t selectorReferenceRuntimeOffset, uint64_t selectorStringRuntimeOffset));

    // Intermediate data which doesn't get saved to the PrebuiltLoader(Set)
    dyld3::OverflowSafeArray<ObjCOptimizerImage>                            objcImages;
    dyld3::OverflowSafeArray<const char*>                                   closureSelectorStrings;
    SelectorMapTy                                                           closureSelectorMap;
    DuplicateClassesMapTy                                                   duplicateSharedCacheClassMap;
    ObjCStringTable*                                                        selectorStringTable             = nullptr;
    bool                                                                    builtObjC                       = false;

    // These data structures all get saved to the PrebuiltLoaderSet
    dyld3::OverflowSafeArray<uint8_t>               selectorsHashTable;
    dyld3::OverflowSafeArray<uint8_t>               classesHashTable;
    dyld3::OverflowSafeArray<uint8_t>               protocolsHashTable;
    uint64_t                                        objcProtocolClassCacheOffset = 0;

    // Per-image info, which is saved to the PrebuiltLoader's
    struct ObjCImageFixups {
        ObjCBinaryInfo                                          binaryInfo;
        dyld3::OverflowSafeArray<uint8_t>                       protocolISAFixups;
        dyld3::OverflowSafeArray<PrebuiltLoader::BindTargetRef> selectorReferenceFixups;
    };

    // Indexed by the app Loader index
    dyld3::OverflowSafeArray<ObjCImageFixups> imageFixups;
};

} // namespace dyld4

// Temporary copy of the old hash tables, to let the split cache branch load old hash tables
namespace legacy_objc_opt
{

typedef int32_t objc_stringhash_offset_t;
typedef uint8_t objc_stringhash_check_t;

// Precomputed perfect hash table of strings.
// Base class for precomputed selector table and class table.
// Edit objc-sel-table.s if you change this structure.
struct __attribute__((packed)) objc_stringhash_t {
    uint32_t capacity;
    uint32_t occupied;
    uint32_t shift;
    uint32_t mask;
    uint32_t unused1;  // was zero
    uint32_t unused2;  // alignment pad
    uint64_t salt;

    uint32_t scramble[256];
    uint8_t tab[0];                   /* tab[mask+1] (always power-of-2) */
    // uint8_t checkbytes[capacity];  /* check byte for each string */
    // int32_t offsets[capacity];     /* offsets from &capacity to cstrings */

    objc_stringhash_check_t *checkbytes() { return (objc_stringhash_check_t *)&tab[mask+1]; }
    const objc_stringhash_check_t *checkbytes() const { return (const objc_stringhash_check_t *)&tab[mask+1]; }

    objc_stringhash_offset_t *offsets() { return (objc_stringhash_offset_t *)&checkbytes()[capacity]; }
    const objc_stringhash_offset_t *offsets() const { return (const objc_stringhash_offset_t *)&checkbytes()[capacity]; }

    uint32_t hash(const char *key, size_t keylen) const;

    uint32_t hash(const char *key) const
    {
        return hash(key, strlen(key));
    }

    // The check bytes areused to reject strings that aren't in the table
    // without paging in the table's cstring data. This checkbyte calculation
    // catches 4785/4815 rejects when launching Safari; a perfect checkbyte
    // would catch 4796/4815.
    objc_stringhash_check_t checkbyte(const char *key, size_t keylen) const
    {
        return
            ((key[0] & 0x7) << 5)
            |
            ((uint8_t)keylen & 0x1f);
    }

    objc_stringhash_check_t checkbyte(const char *key) const
    {
        return checkbyte(key, strlen(key));
    }


#define INDEX_NOT_FOUND (~(uint32_t)0)

    uint32_t getIndex(const char *key) const
    {
        size_t keylen = strlen(key);
        uint32_t h = hash(key, keylen);

        // Use check byte to reject without paging in the table's cstrings
        objc_stringhash_check_t h_check = checkbytes()[h];
        objc_stringhash_check_t key_check = checkbyte(key, keylen);
        bool check_fail = (h_check != key_check);
#if ! SELOPT_DEBUG
        if (check_fail) return INDEX_NOT_FOUND;
#endif

        objc_stringhash_offset_t offset = offsets()[h];
        if (offset == 0) return INDEX_NOT_FOUND;
        const char *result = (const char *)this + offset;
        if (0 != strcmp(key, result)) return INDEX_NOT_FOUND;

#if SELOPT_DEBUG
        if (check_fail) abort();
#endif

        return h;
    }
};

// Precomputed selector table.
// Edit objc-sel-table.s if you change this structure.
struct objc_selopt_t : objc_stringhash_t {

    const char* getEntryForIndex(uint32_t index) const {
        return (const char *)this + offsets()[index];
    }

    uint32_t getIndexForKey(const char *key) const {
        return getIndex(key);
    }

    uint32_t getSentinelIndex() const {
        return INDEX_NOT_FOUND;
    }

    const char* get(const char *key) const
    {
        uint32_t h = getIndex(key);
        if (h == INDEX_NOT_FOUND) return NULL;
        return getEntryForIndex(h);
    }

    size_t usedCount() const {
        return capacity;
    }
};

struct objc_classheader_t {
    objc_stringhash_offset_t clsOffset;
    objc_stringhash_offset_t hiOffset;

    // For duplicate class names:
    // clsOffset = count<<1 | 1
    // duplicated classes are duplicateOffsets[hiOffset..hiOffset+count-1]
    bool isDuplicate() const { return clsOffset & 1; }
    uint32_t duplicateCount() const { return clsOffset >> 1; }
    uint32_t duplicateIndex() const { return hiOffset; }
};

struct objc_clsopt_t : objc_stringhash_t {
    // ...objc_stringhash_t fields...
    // objc_classheader_t classOffsets[capacity]; /* offsets from &capacity to class_t and header_info */
    // uint32_t duplicateCount;
    // objc_classheader_t duplicateOffsets[duplicatedClasses];

    objc_classheader_t *classOffsets() { return (objc_classheader_t *)&offsets()[capacity]; }
    const objc_classheader_t *classOffsets() const { return (const objc_classheader_t *)&offsets()[capacity]; }

    uint32_t& duplicateCount() { return *(uint32_t *)&classOffsets()[capacity]; }
    const uint32_t& duplicateCount() const { return *(const uint32_t *)&classOffsets()[capacity]; }

    objc_classheader_t *duplicateOffsets() { return (objc_classheader_t *)(&duplicateCount()+1); }
    const objc_classheader_t *duplicateOffsets() const { return (const objc_classheader_t *)(&duplicateCount()+1); }

    uint32_t classCount() const {
        return occupied + duplicateCount();
    }

    const char* getClassNameForIndex(uint32_t index) const {
        return (const char *)this + offsets()[index];
    }

    void* getClassForIndex(uint32_t index, uint32_t duplicateIndex) const {
        const objc_classheader_t& clshi = classOffsets()[index];
        if (! clshi.isDuplicate()) {
            // class appears in exactly one header
            return (void *)((const char *)this + clshi.clsOffset);
        }
        else {
            // class appears in more than one header - use getClassesAndHeaders
            const objc_classheader_t *list = &duplicateOffsets()[clshi.duplicateIndex()];
            return (void *)((const char *)this + list[duplicateIndex].clsOffset);
        }
    }

    // 0/NULL/NULL: not found
    // 1/ptr/ptr: found exactly one
    // n/NULL/NULL:  found N - use getClassesAndHeaders() instead
    uint32_t getClassHeaderAndIndex(const char *key, void*& cls, void*& hi, uint32_t& index) const
    {
        uint32_t h = getIndex(key);
        if (h == INDEX_NOT_FOUND) {
            cls = NULL;
            hi = NULL;
            index = 0;
            return 0;
        }

        index = h;

        const objc_classheader_t& clshi = classOffsets()[h];
        if (! clshi.isDuplicate()) {
            // class appears in exactly one header
            cls = (void *)((const char *)this + clshi.clsOffset);
            hi  = (void *)((const char *)this + clshi.hiOffset);
            return 1;
        }
        else {
            // class appears in more than one header - use getClassesAndHeaders
            cls = NULL;
            hi = NULL;
            return clshi.duplicateCount();
        }
    }

    void getClassesAndHeaders(const char *key, void **cls, void **hi) const
    {
        uint32_t h = getIndex(key);
        if (h == INDEX_NOT_FOUND) return;

        const objc_classheader_t& clshi = classOffsets()[h];
        if (! clshi.isDuplicate()) {
            // class appears in exactly one header
            cls[0] = (void *)((const char *)this + clshi.clsOffset);
            hi[0]  = (void *)((const char *)this + clshi.hiOffset);
        }
        else {
            // class appears in more than one header
            uint32_t count = clshi.duplicateCount();
            const objc_classheader_t *list =
                &duplicateOffsets()[clshi.duplicateIndex()];
            for (uint32_t i = 0; i < count; i++) {
                cls[i] = (void *)((const char *)this + list[i].clsOffset);
                hi[i]  = (void *)((const char *)this + list[i].hiOffset);
            }
        }
    }

    // 0/NULL/NULL: not found
    // 1/ptr/ptr: found exactly one
    // n/NULL/NULL:  found N - use getClassesAndHeaders() instead
    uint32_t getClassAndHeader(const char *key, void*& cls, void*& hi) const
    {
        uint32_t unusedIndex = 0;
        return getClassHeaderAndIndex(key, cls, hi, unusedIndex);
    }

    void forEachClass(void (^callback)(const dyld3::Array<const void*>& classes)) const {
        for ( unsigned i = 0; i != capacity; ++i ) {
            objc_stringhash_offset_t nameOffset = offsets()[i];
            if ( nameOffset == 0 )
                continue;

            // Walk each class for this key
            const objc_classheader_t& data = classOffsets()[i];
            if ( !data.isDuplicate() ) {
                // This class/protocol has a single implementation
                const void* cls = (void *)((const char *)this + data.clsOffset);
                const dyld3::Array<const void*> classes(&cls, 1, 1);
                callback(classes);
            }
            else {
                // This class/protocol has mulitple implementations.
                uint32_t count = data.duplicateCount();
                const void* cls[count];
                const objc_classheader_t* list  = &duplicateOffsets()[data.duplicateIndex()];
                for (uint32_t duplicateIndex = 0; duplicateIndex < count; duplicateIndex++) {
                    cls[duplicateIndex] = (void *)((const char *)this + list[i].clsOffset);
                }
                const dyld3::Array<const void*> classes(&cls[0], count, count);
                callback(classes);
            }
        }
    }
};

struct header_info_rw {

    bool getLoaded() const {
        return isLoaded;
    }

private:
#ifdef __LP64__
    [[maybe_unused]] uintptr_t isLoaded              : 1;
    [[maybe_unused]] uintptr_t allClassesRealized    : 1;
    [[maybe_unused]] uintptr_t next                  : 62;
#else
    [[maybe_unused]] uintptr_t isLoaded              : 1;
    [[maybe_unused]] uintptr_t allClassesRealized    : 1;
    [[maybe_unused]] uintptr_t next                  : 30;
#endif
};

const struct header_info_rw* getPreoptimizedHeaderRW(const struct header_info *const hdr,
                                                     void* headerInfoRO, void* headerInfoRW);

struct header_info {
private:
    // Note, this is no longer a pointer, but instead an offset to a pointer
    // from this location.
    intptr_t mhdr_offset;

    // Note, this is no longer a pointer, but instead an offset to a pointer
    // from this location.
    [[maybe_unused]] intptr_t info_offset;

public:

    const header_info_rw *getHeaderInfoRW(void* headerInfoRO, void* headerInfoRW) {
        return getPreoptimizedHeaderRW(this, headerInfoRO, headerInfoRW);
    }

    const void *mhdr() const {
        return (const void *)(((intptr_t)&mhdr_offset) + mhdr_offset);
    }

    bool isLoaded(void* headerInfoRO, void* headerInfoRW) {
        return getHeaderInfoRW(headerInfoRO, headerInfoRW)->getLoaded();
    }
};

struct objc_headeropt_ro_t {
    uint32_t count;
    uint32_t entsize;
    header_info headers[0];  // sorted by mhdr address

    header_info& getOrEnd(uint32_t i) const {
        assert(i <= count);
        return *(header_info *)((uint8_t *)&headers + (i * entsize));
    }

    header_info& get(uint32_t i) const {
        assert(i < count);
        return *(header_info *)((uint8_t *)&headers + (i * entsize));
    }

    uint32_t index(const header_info* hi) const {
        const header_info* begin = &get(0);
        const header_info* end = &getOrEnd(count);
        assert(hi >= begin && hi < end);
        return (uint32_t)(((uintptr_t)hi - (uintptr_t)begin) / entsize);
    }

    header_info *get(const void *mhdr)
    {
        int32_t start = 0;
        int32_t end = count;
        while (start <= end) {
            int32_t i = (start+end)/2;
            header_info &hi = get(i);
            if (mhdr == hi.mhdr()) return &hi;
            else if (mhdr < hi.mhdr()) end = i-1;
            else start = i+1;
        }

        return nullptr;
    }
};

struct objc_headeropt_rw_t {
    uint32_t count;
    uint32_t entsize;
    header_info_rw headers[0];  // sorted by mhdr address
};

};


#endif // PreBuiltObjC_h


