/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
 *
 * Copyright (c) 2014 Apple Inc. All rights reserved.
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

//                                 Swift Optimizations
//
// The shared cache Swift optimizations are designed to speed up protocol conformance
// lookups.
//
// Protocol conformances are stored as an array on each dylib.  To find out if a type conforms
// to a protocol, Swift must walk these arrays in all loaded dylibs.  This is then cached in
// the Swift runtime.
//
// This optimization builds a number of hash tables to speed up these lookups, and allows the
// Swift runtime to avoid caching the results from these tables.  This saves both time and memory.
//
// We start by finding all protocol conformances by walking the "__TEXT, __swift5_proto" section.
// There are several kinds of conformance:
//   1) (type*, protocol*)
//   2) (objc_class*, protocol*)
//   3) (class name*, protocol*)
//   4) (foreign metadata name*, protocol*)
//
// 1) Type Pointers
//
// These are made up of a pointer to a type, and a pointer to a protocol.
// We turn these in to shared cache offsets for the type, protocol, conformance,
// and the index of the dylib containing the conformance.  See SwiftTypeProtocolConformanceLocation.
// At runtime, we look in the table at typeConformanceHashTableCacheOffset, to see if a given type and
// protocol are in the table, and if the conformance is from a loaded image.
// Note it is possible for this table to contain duplicates.  In this case, we return the first found
// conformance, in the order we found them in the shared cache.
//
// 2) ObjC Class Pointers
//
// These are similar to type pointers, but are classed as metadata in the Swift runtime.
// Again, similarly to the above, we convert the metadata, protocol, and conformance pointers to
// shared cache offsets.  See SwiftForeignTypeProtocolConformanceLocationKey.
// At runtime, we may be passed a non-null metadata pointer.  In that case, we search the table
// reached via metadataConformanceHashTableCacheOffset, for matching a ObjC Class and Protocol,
// and check that the conformance dylib is loaded.  Again duplicates are supported.
//
// 3) ObjC Class Names
//
// In this case, we have the "const char*" name of the ObjC class to lookup.  The Swift runtime does
// this by asking the ObjC runtime for the Class with this name.  In the shared cache, we use the ObjC
// class hash table to find the Class pointers for all classes with the given name.  As we won't know
// which one is loaded, we record them all, so duplicates are likely to happen here.
// The Class pointers we find from the ObjC hash table are converted to shared cache offsets, and stored
// in the same hash table as 2) above.  All other details in 2) apply.
//
// 4) Foreign Metadata Names
//
// These names are found via the Type Pointers in 1).  We visiting a TypeDescriptor, we may
// find it has an attached Foreign Name.  This is used when the Swift runtime wants to unique a Type by
// name, not by pointer.
// In this case, names and their protocols are converted to cache offsets and stored in the hash table
// found via foreignTypeConformanceHashTableCacheOffset.
// At runtime, the Swift runtime will pass a name and protocol to look up in this table.
//
// Foreign metadata names may additionally have "ImportInfo", which describes an alternative name to use.
// This alternative name is the key we store in the map.  It can be found by the getForeignFullIdentity() method.
// The Swift runtime also knows if metadata has one of these "Full Identities", and will always pass in the
// Full Identity when calling the SPI.  At runtime, dyld does not know that a given entry in the map is
// a regular Foreign metadata name, or the Full Identity.
//
// One final quirk of Full Identity names, is that they can contain null characters.  Eg, NNSFoo\0St.
// Given this, all of the code to handle foreign metadata names, including lookups in the hash table, and
// the SPI below, take name and name length.  We never assume that the name is a null-terminated C string.
//
// SPIs
//
// The above types are stored in 3 tables: Type, Metadata, Foreign Metadata.
// These are accessed by 2 different SPIs.
//
// _dyld_find_protocol_conformance()
//
// This searches for types and metadata.  It takes Type* and Metadata* arguments
// and looks up the corresponding table, depending on which of Type* or Metadata*
// is non-null.
//
// _dyld_find_foreign_type_protocol_conformance()
//
// This looks up the given name in the Foreign Metadata table.  Matches are done
// by string comparison.  As noted above in 4), the name may contain null characters
// so all hashing, etc, is done with std::string_view which allows null characters.


#include "DyldSharedCache.h"
#include "Diagnostics.h"
#include "MachOLoaded.h"
#include "MachOAnalyzer.h"
#include "OptimizerObjC.h"
#include "OptimizerSwift.h"
#include "PerfectHash.h"

#if BUILDING_CACHE_BUILDER
#include "SharedCacheBuilder.h"
#include "objc-shared-cache.h"
#endif

typedef dyld3::MachOAnalyzer::SwiftProtocolConformance SwiftProtocolConformance;

// Tracks which types conform to which protocols

namespace std {
    template<>
    struct hash<SwiftTypeProtocolConformanceLocationKey>
    {
        size_t operator()(const SwiftTypeProtocolConformanceLocationKey& v) const {
            return std::hash<uint64_t>{}(v.typeDescriptorCacheOffset) ^ std::hash<uint64_t>{}(v.protocolCacheOffset);
        }
    };

    template<>
    struct equal_to<SwiftTypeProtocolConformanceLocationKey>
    {
        bool operator()(const SwiftTypeProtocolConformanceLocationKey& a,
                        const SwiftTypeProtocolConformanceLocationKey& b) const {
            return a.typeDescriptorCacheOffset == b.typeDescriptorCacheOffset && a.protocolCacheOffset == b.protocolCacheOffset;
        }
    };
}

// Tracks which Metadata conform to which protocols

namespace std {
    template<>
    struct hash<SwiftMetadataProtocolConformanceLocationKey>
    {
        size_t operator()(const SwiftMetadataProtocolConformanceLocationKey& v) const {
            return std::hash<uint64_t>{}(v.metadataCacheOffset) ^ std::hash<uint64_t>{}(v.protocolCacheOffset);
        }
    };

    template<>
    struct equal_to<SwiftMetadataProtocolConformanceLocationKey>
    {
        bool operator()(const SwiftMetadataProtocolConformanceLocationKey& a,
                        const SwiftMetadataProtocolConformanceLocationKey& b) const {
            return a.metadataCacheOffset == b.metadataCacheOffset && a.protocolCacheOffset == b.protocolCacheOffset;
        }
    };
}

// Tracks which foreign types conform to which protocols

namespace std {
    template<>
    struct hash<SwiftForeignTypeProtocolConformanceLocationKey>
    {
        size_t operator()(const SwiftForeignTypeProtocolConformanceLocationKey& v) const {
            return std::hash<uint64_t>{}(v.rawForeignDescriptor) ^ std::hash<uint64_t>{}(v.protocolCacheOffset);
        }
    };

    template<>
    struct equal_to<SwiftForeignTypeProtocolConformanceLocationKey>
    {
        bool operator()(const SwiftForeignTypeProtocolConformanceLocationKey& a,
                        const SwiftForeignTypeProtocolConformanceLocationKey& b) const {
            return a.rawForeignDescriptor == b.rawForeignDescriptor && a.protocolCacheOffset == b.protocolCacheOffset;
        }
    };
}

// Type Hash Table methods
template<>
uint32_t SwiftHashTable::hash(const SwiftTypeProtocolConformanceLocationKey& key,
                              const uint8_t*) const {
    uint64_t val1 = objc::lookup8(key.key1Buffer(nullptr), key.key1Size(), salt);
    uint64_t val2 = objc::lookup8((uint8_t*)&key.protocolCacheOffset, sizeof(key.protocolCacheOffset), salt);
    uint64_t val = val1 ^ val2;
    uint32_t index = (uint32_t)((shift == 64) ? 0 : (val>>shift)) ^ scramble[tab[val&mask]];
    return index;
}


template<>
bool SwiftHashTable::equal(const SwiftTypeProtocolConformanceLocationKey& key, const SwiftTypeProtocolConformanceLocationKey& value,
                           const uint8_t*) const {
    return memcmp(&key, &value, sizeof(SwiftTypeProtocolConformanceLocationKey)) == 0;
}

template<>
SwiftHashTable::CheckByteType SwiftHashTable::checkbyte(const SwiftTypeProtocolConformanceLocationKey& key, const uint8_t*) const
{
    const uint8_t* keyBytes = (const uint8_t*)&key;
    return ((keyBytes[0] & 0x7) << 5) | ((uint8_t)sizeof(SwiftTypeProtocolConformanceLocationKey) & 0x1f);
}

// Metadata Hash Table methods
template<>
uint32_t SwiftHashTable::hash(const SwiftMetadataProtocolConformanceLocationKey& key,
                              const uint8_t*) const {
    uint64_t val1 = objc::lookup8(key.key1Buffer(nullptr), key.key1Size(), salt);
    uint64_t val2 = objc::lookup8((uint8_t*)&key.protocolCacheOffset, sizeof(key.protocolCacheOffset), salt);
    uint64_t val = val1 ^ val2;
    uint32_t index = (uint32_t)((shift == 64) ? 0 : (val>>shift)) ^ scramble[tab[val&mask]];
    return index;
}


template<>
bool SwiftHashTable::equal(const SwiftMetadataProtocolConformanceLocationKey& key, const SwiftMetadataProtocolConformanceLocationKey& value,
                           const uint8_t*) const {
    return memcmp(&key, &value, sizeof(SwiftMetadataProtocolConformanceLocationKey)) == 0;
}

template<>
SwiftHashTable::CheckByteType SwiftHashTable::checkbyte(const SwiftMetadataProtocolConformanceLocationKey& key, const uint8_t*) const
{
    const uint8_t* keyBytes = (const uint8_t*)&key;
    return ((keyBytes[0] & 0x7) << 5) | ((uint8_t)sizeof(SwiftTypeProtocolConformanceLocationKey) & 0x1f);
}

// Foreign Type Hash Table methods
template<>
uint32_t SwiftHashTable::hash(const SwiftForeignTypeProtocolConformanceLocationKey& key,
                              const uint8_t* stringBaseAddress) const {
    // Combine the hashes of the foreign type string and the protocol cache offset.
    // Then combine them to get the hash for this value
    const char* name = (const char*)stringBaseAddress + key.foreignDescriptorNameCacheOffset;
    uint64_t val1 = objc::lookup8((uint8_t*)name, key.foreignDescriptorNameLength, salt);
    uint64_t val2 = objc::lookup8((uint8_t*)&key.protocolCacheOffset, sizeof(key.protocolCacheOffset), salt);
    uint64_t val = val1 ^ val2;
    uint32_t index = (uint32_t)((shift == 64) ? 0 : (val>>shift)) ^ scramble[tab[val&mask]];
    return index;
}


template<>
bool SwiftHashTable::equal(const SwiftForeignTypeProtocolConformanceLocationKey& key, const SwiftForeignTypeProtocolConformanceLocationKey& value,
                           const uint8_t*) const {
    return memcmp(&key, &value, sizeof(SwiftForeignTypeProtocolConformanceLocationKey)) == 0;
}

template<>
SwiftHashTable::CheckByteType SwiftHashTable::checkbyte(const SwiftForeignTypeProtocolConformanceLocationKey& key, const uint8_t* stringBaseAddress) const
{
    const char* name = (const char*)stringBaseAddress + key.foreignDescriptorNameCacheOffset;
    const uint8_t* keyBytes = (const uint8_t*)name;
    return ((keyBytes[0] & 0x7) << 5) | ((uint8_t)key.foreignDescriptorNameLength & 0x1f);
}

// Foreign Type Hash Table methods, using a string as a key
template<>
uint32_t SwiftHashTable::hash(const SwiftForeignTypeProtocolConformanceLookupKey& key,
                              const uint8_t* stringBaseAddress) const {
    // Combine the hashes of the foreign type string and the protocol cache offset.
    // Then combine them to get the hash for this value
    const std::string_view& name = key.foreignDescriptorName;
    uint64_t val1 = objc::lookup8((uint8_t*)name.data(), name.size(), salt);
    uint64_t val2 = objc::lookup8((uint8_t*)&key.protocolCacheOffset, sizeof(key.protocolCacheOffset), salt);
    uint64_t val = val1 ^ val2;
    uint32_t index = (uint32_t)((shift == 64) ? 0 : (val>>shift)) ^ scramble[tab[val&mask]];
    return index;
}


template<>
bool SwiftHashTable::equal(const SwiftForeignTypeProtocolConformanceLocationKey& key, const SwiftForeignTypeProtocolConformanceLookupKey& value,
                           const uint8_t* stringBaseAddress) const {
    std::string_view keyName((const char*)key.key1Buffer(stringBaseAddress), key.key1Size());
    return (key.protocolCacheOffset == value.protocolCacheOffset) && (keyName == value.foreignDescriptorName);
}

template<>
SwiftHashTable::CheckByteType SwiftHashTable::checkbyte(const SwiftForeignTypeProtocolConformanceLookupKey& key, const uint8_t* stringBaseAddress) const
{
    const std::string_view& name = key.foreignDescriptorName;
    const uint8_t* keyBytes = (const uint8_t*)name.data();
    return ((keyBytes[0] & 0x7) << 5) | ((uint8_t)name.size() & 0x1f);
}

#if BUILDING_CACHE_BUILDER

// Swift hash tables
template<typename TargetT>
static void make_perfect(const std::vector<TargetT> targets, const uint8_t* stringBaseAddress,
                         objc::PerfectHash& phash)
{
    dyld3::OverflowSafeArray<objc::PerfectHash::key> keys;

    /* read in the list of keywords */
    keys.reserve(targets.size());
    for (const TargetT& target : targets) {
        objc::PerfectHash::key mykey;
        mykey.name1_k = (uint8_t*)target.key1Buffer(stringBaseAddress);
        mykey.len1_k  = (uint32_t)target.key1Size();
        mykey.name2_k = (uint8_t*)target.key2Buffer(stringBaseAddress);
        mykey.len2_k  = (uint32_t)target.key2Size();
        keys.push_back(mykey);
    }

    objc::PerfectHash::make_perfect(keys, phash);
}

template<typename PerfectHashT, typename TargetT>
void SwiftHashTable::write(const PerfectHashT& phash, const std::vector<TargetT>& targetValues,
                           const uint8_t* targetValuesBufferBaseAddress,
                           const uint8_t* stringBaseAddress)
{
    // Set header
    capacity = phash.capacity;
    occupied = phash.occupied;
    shift = phash.shift;
    mask = phash.mask;
    sentinelTarget = sentinel;
    roundedTabSize = std::max(phash.mask+1, 4U);
    salt = phash.salt;

    // Set hash data
    for (uint32_t i = 0; i < 256; i++) {
        scramble[i] = phash.scramble[i];
    }
    for (uint32_t i = 0; i < phash.mask+1; i++) {
        tab[i] = phash.tab[i];
    }

    dyld3::Array<TargetOffsetType> targetsArray = targets();
    dyld3::Array<CheckByteType> checkBytesArray = checkBytes();

    // Set offsets to the sentinel
    for (uint32_t i = 0; i < phash.capacity; i++) {
        targetsArray[i] = sentinel;
    }
    // Set checkbytes to 0
    for (uint32_t i = 0; i < phash.capacity; i++) {
        checkBytesArray[i] = 0;
    }

    // Set real value offsets and checkbytes
    uint32_t offsetOfTargetBaseFromMap = (uint32_t)((uint64_t)targetValuesBufferBaseAddress - (uint64_t)this);
    bool skipNext = false;
    for (const TargetT& targetValue : targetValues) {
        // Skip chains of duplicates
        bool skipThisEntry = skipNext;
        skipNext = targetValue.nextIsDuplicate;
        if ( skipThisEntry )
            continue;

        uint32_t h = hash<typename TargetT::KeyType>(targetValue, stringBaseAddress);
        uint32_t offsetOfTargetValueInArray = (uint32_t)((uint64_t)&targetValue - (uint64_t)targetValues.data());
        assert(targetsArray[h] == sentinel);
        targetsArray[h] = offsetOfTargetBaseFromMap + offsetOfTargetValueInArray;
        assert(checkBytesArray[h] == 0);
        checkBytesArray[h] = checkbyte<typename TargetT::KeyType>(targetValue, stringBaseAddress);
    }
}

// Map from an unsigned 32-bit type to its signed counterpart.
// Used for offset calculations
template<typename PointerType>
struct OffsetType {
};

template<>
struct OffsetType<uint32_t> {
    typedef int32_t SignedType;
};

template<>
struct OffsetType<uint64_t> {
    typedef int64_t SignedType;
};

template <typename PointerType>
struct header_info_rw {
};

template<>
struct header_info_rw<uint64_t> {

    bool getLoaded() const {
        return isLoaded;
    }

private:
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
    uint64_t isLoaded              : 1;
    uint64_t allClassesRealized    : 1;
    uint64_t next                  : 62;
#pragma clang diagnostic pop
};

template<>
struct header_info_rw<uint32_t> {

    bool getLoaded() const {
        return isLoaded;
    }

private:
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-private-field"
    uint32_t isLoaded              : 1;
    uint32_t allClassesRealized    : 1;
    uint32_t next                  : 30;
#pragma clang diagnostic pop
};

template <typename PointerType>
class objc_header_info_ro_t {
private:
    PointerType mhdr_offset;     // offset to mach_header or mach_header_64
    PointerType info_offset;     // offset to objc_image_info *

public:
    const mach_header* mhdr() const {
        typedef typename OffsetType<PointerType>::SignedType SignedType;
        return (const mach_header*)(((intptr_t)&mhdr_offset) + (SignedType)mhdr_offset);
    }
};

template <typename PointerType>
struct objc_headeropt_ro_t {
    uint32_t count;
    uint32_t entsize;
    objc_header_info_ro_t<PointerType> headers[0];  // sorted by mhdr address

    objc_header_info_ro_t<PointerType>& getOrEnd(uint32_t i) const {
        assert(i <= count);
        return *(objc_header_info_ro_t<PointerType>*)((uint8_t *)&headers + (i * entsize));
    }

    objc_header_info_ro_t<PointerType>& get(uint32_t i) const {
        assert(i < count);
        return *(objc_header_info_ro_t<PointerType>*)((uint8_t *)&headers + (i * entsize));
    }

    uint32_t index(const objc_header_info_ro_t<PointerType>* hi) const {
        const objc_header_info_ro_t<PointerType>* begin = &get(0);
        const objc_header_info_ro_t<PointerType>* end = &getOrEnd(count);
        assert(hi >= begin && hi < end);
        return (uint32_t)(((uintptr_t)hi - (uintptr_t)begin) / entsize);
    }

    objc_header_info_ro_t<PointerType>* get(const mach_header* mhdr)
    {
        int32_t start = 0;
        int32_t end = count;
        while (start <= end) {
            int32_t i = (start+end)/2;
            objc_header_info_ro_t<PointerType> &hi = get(i);
            if (mhdr == hi.mhdr()) return &hi;
            else if (mhdr < hi.mhdr()) end = i-1;
            else start = i+1;
        }

        return nullptr;
    }
};

template <typename PointerType>
struct objc_headeropt_rw_t {
    uint32_t count;
    uint32_t entsize;
    header_info_rw<PointerType> headers[0];  // sorted by mhdr address

    void* get(uint32_t i) const {
        assert(i < count);
        return (void*)((uint8_t *)&headers + (i * entsize));
    }
};

static std::optional<uint16_t> getPreoptimizedHeaderRWIndex(const void* headerInfoRO, const void* headerInfoRW, const dyld3::MachOAnalyzer* ma)
{
    assert(headerInfoRO != nullptr);
    assert(headerInfoRW != nullptr);
    if ( ma->is64() ) {
        typedef uint64_t PointerType;
        objc_headeropt_ro_t<PointerType>* hinfoRO = (objc_headeropt_ro_t<PointerType>*)headerInfoRO;
        objc_headeropt_rw_t<PointerType>* hinfoRW = (objc_headeropt_rw_t<PointerType>*)headerInfoRW;

        objc_header_info_ro_t<PointerType>* hdr = hinfoRO->get(ma);
        if ( hdr == nullptr )
            return {};
        int32_t index = hinfoRO->index(hdr);
        assert(hinfoRW->entsize == sizeof(header_info_rw<PointerType>));
        return (uint16_t)index;
    } else {
        typedef uint32_t PointerType;
        objc_headeropt_ro_t<PointerType>* hinfoRO = (objc_headeropt_ro_t<PointerType>*)headerInfoRO;
        objc_headeropt_rw_t<PointerType>* hinfoRW = (objc_headeropt_rw_t<PointerType>*)headerInfoRW;

        objc_header_info_ro_t<PointerType>* hdr = hinfoRO->get(ma);
        if ( hdr == nullptr )
            return {};
        int32_t index = hinfoRO->index(hdr);
        assert(hinfoRW->entsize == sizeof(header_info_rw<PointerType>));
        return (uint16_t)index;
    }
}

// Foreign metadata names might not be a regular C string.  Instead they might be
// a NULL-separated array of C strings.  The "full identity" is the result including any
// intermidiate NULL characters.  Eg, "NNSFoo\0St" would be a legitimate result
static std::string_view getForeignFullIdentity(const char* arrayStart)
{
    // Track the extent of the current component.
    const char* componentStart = arrayStart;
    const char* componentEnd = componentStart + strlen(arrayStart);

    // Set initial range to the extent of the user-facing name.
    const char* identityBeginning = componentStart;
    const char* identityEnd = componentEnd;

    // Start examining the following array components, starting past the NUL
    // terminator of the user-facing name:
    while (true) {
        // Advance past the NUL terminator.
        componentStart = componentEnd + 1;
        componentEnd = componentStart + strlen(componentStart);

        // If the component is empty, then we're done.
        if (componentStart == componentEnd)
            break;

        // Switch on the component type at the beginning of the component.
        switch (componentStart[0]) {
            case 'N':
                // ABI name, set identity beginning and end.
                identityBeginning = componentStart + 1;
                identityEnd = componentEnd;
                break;
            case 'S':
            case 'R':
                // Symbol namespace or related entity name, set identity end.
                identityEnd = componentEnd;
                break;
            default:
                // Ignore anything else.
                break;
        }
    }

    size_t stringSize = identityEnd - identityBeginning;
    return std::string_view(identityBeginning, stringSize);
}

static bool findProtocolConformances(Diagnostics& diags, const DyldSharedCache* dyldCache,
                                     std::vector<SwiftTypeProtocolConformanceLocation>& foundTypeProtocolConformances,
                                     std::vector<SwiftMetadataProtocolConformanceLocation>& foundMetadataProtocolConformances,
                                     std::vector<SwiftForeignTypeProtocolConformanceLocation>& foundForeignTypeProtocolConformances)
{
    // If we have the read only data, make sure it has a valid selector table inside.
    const objc_opt::objc_opt_t* optObjCHeader = dyldCache->objcOpt();
    const objc::ClassHashTable* classHashTable = nullptr;
    if ( optObjCHeader != nullptr ) {
        classHashTable = optObjCHeader->classOpt();
    }

    if ( classHashTable == nullptr ) {
        diags.warning("Skipped optimizing Swift protocols due to missing objc class optimisations");
        return false;
    }

    const void* headerInfoRO = (const void*)optObjCHeader->headeropt_ro();
    const void* headerInfoRW = (const void*)optObjCHeader->headeropt_rw();
    if ( (headerInfoRO == nullptr) || (headerInfoRW == nullptr) ) {
        diags.warning("Skipped optimizing Swift protocols due to missing objc header infos");
        return false;
    }

    const bool log = false;

    // Find all conformances in all binaries
    dyldCache->forEachImage(^(const mach_header* machHeader, const char* installName) {

        if ( diags.hasError() )
            return;

        const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)machHeader;

        auto vmAddrConverter = ma->makeVMAddrConverter(true);
        // HACK: At this point in the builder, everything contains vmAddr's.  Setting
        // the above vmAddrConverter as "rebabed" and a 0 slide, causes nothing to be converted later
        vmAddrConverter.slide = 0;

        uint64_t binaryCacheOffset = (uint64_t)ma - (uint64_t)dyldCache;

        __block std::unordered_map<uint64_t, const char*> symbols;
        if ( log ) {
            uint64_t baseAddress = ma->preferredLoadAddress();
            ma->forEachGlobalSymbol(diags, ^(const char *symbolName, uint64_t n_value, uint8_t n_type, uint8_t n_sect, uint16_t n_desc, bool &stop) {
                symbols[n_value - baseAddress] = symbolName;
            });
        }

        ma->forEachSwiftProtocolConformance(diags, vmAddrConverter, true,
                                            ^(uint64_t protocolConformanceRuntimeOffset, const SwiftProtocolConformance &protocolConformance,
                                              bool &stopProtocolConformance) {

            std::optional<uint16_t> objcIndex = getPreoptimizedHeaderRWIndex(headerInfoRO, headerInfoRW, ma);
            if ( !objcIndex.has_value() ) {
                diags.error("Could not find objc header info for Swift dylib: %s", installName);
                stopProtocolConformance = true;
                return;
            }

            uint16_t dylibObjCIndex = *objcIndex;

            // The type descriptor might be a pointer to an objc name/class.  If so, we need to translate that in to a pointer to a type descriptor
            // For now just skip adding found protocols to objc
            if ( protocolConformance.typeConformanceRuntimeOffset != 0 ) {
                SwiftTypeProtocolConformanceLocation protoLoc;
                protoLoc.protocolConformanceCacheOffset = binaryCacheOffset + protocolConformanceRuntimeOffset;
                protoLoc.dylibObjCIndex = dylibObjCIndex;
                protoLoc.typeDescriptorCacheOffset = binaryCacheOffset + protocolConformance.typeConformanceRuntimeOffset;
                protoLoc.protocolCacheOffset = binaryCacheOffset + protocolConformance.protocolRuntimeOffset;
                foundTypeProtocolConformances.push_back(protoLoc);
                if ( log ) {
                    const char* typeName = "";
                    const char* protocolName = "";
                    const char* conformanceName = "";
                    if ( auto it = symbols.find(protocolConformance.typeConformanceRuntimeOffset); it != symbols.end() )
                        typeName = it->second;
                    if ( auto it = symbols.find(protocolConformance.protocolRuntimeOffset); it != symbols.end() )
                        protocolName = it->second;
                    if ( auto it = symbols.find(protocolConformanceRuntimeOffset); it != symbols.end() )
                        conformanceName = it->second;
                    fprintf(stderr, "%s: (%s, %s) -> %s", ma->installName(), typeName, protocolName, conformanceName);
                }
            } else if ( protocolConformance.typeConformanceObjCClassRuntimeOffset != 0 ) {
                SwiftMetadataProtocolConformanceLocation protoLoc;
                protoLoc.protocolConformanceCacheOffset = binaryCacheOffset + protocolConformanceRuntimeOffset;
                protoLoc.dylibObjCIndex = dylibObjCIndex;
                protoLoc.metadataCacheOffset = binaryCacheOffset + protocolConformance.typeConformanceObjCClassRuntimeOffset;
                protoLoc.protocolCacheOffset = binaryCacheOffset + protocolConformance.protocolRuntimeOffset;
                foundMetadataProtocolConformances.push_back(protoLoc);
                if ( log ) {
                    const char* metadataName = "";
                    const char* protocolName = "";
                    const char* conformanceName = "";
                    if ( auto it = symbols.find(protocolConformance.typeConformanceObjCClassRuntimeOffset); it != symbols.end() )
                        metadataName = it->second;
                    if ( auto it = symbols.find(protocolConformance.protocolRuntimeOffset); it != symbols.end() )
                        protocolName = it->second;
                    if ( auto it = symbols.find(protocolConformanceRuntimeOffset); it != symbols.end() )
                        conformanceName = it->second;
                    fprintf(stderr, "%s: (%s, %s) -> %s", ma->installName(), metadataName, protocolName, conformanceName);
                }
            } else if ( protocolConformance.typeConformanceObjCClassNameRuntimeOffset != 0 ) {
                const char* className = (const char*)ma + protocolConformance.typeConformanceObjCClassNameRuntimeOffset;
                classHashTable->forEachClass(className, ^(uint64_t classCacheOffset, uint16_t dylibObjCIndexForClass, bool &stopClasses) {
                    // exactly one matching class
                    SwiftMetadataProtocolConformanceLocation protoLoc;
                    protoLoc.protocolConformanceCacheOffset = binaryCacheOffset + protocolConformanceRuntimeOffset;
                    protoLoc.dylibObjCIndex = dylibObjCIndex;
                    protoLoc.metadataCacheOffset = classCacheOffset;
                    protoLoc.protocolCacheOffset = binaryCacheOffset + protocolConformance.protocolRuntimeOffset;
                    foundMetadataProtocolConformances.push_back(protoLoc);
                    if ( log ) {
                        const char* protocolName = "";
                        const char* conformanceName = "";
                        if ( auto it = symbols.find(protocolConformance.protocolRuntimeOffset); it != symbols.end() )
                            protocolName = it->second;
                        if ( auto it = symbols.find(protocolConformanceRuntimeOffset); it != symbols.end() )
                            conformanceName = it->second;
                        fprintf(stderr, "%s: (%s, %s) -> %s", ma->installName(), className, protocolName, conformanceName);
                    }
                });
            } else {
                // assert(0 && "Unknown protocol conformance");
                // Missing weak imports can result in us wanting to skip a confornmance.  Assume that is the case here
            }

            // Type's can also have foreign names, which are used to identify the descriptor by name instead of just pointer value
            if ( protocolConformance.foreignMetadataNameRuntimeOffset != 0 ) {
                uint64_t foreignDescriptorNameCacheOffset = binaryCacheOffset + protocolConformance.foreignMetadataNameRuntimeOffset;
                const char* name = (const char*)dyldCache + foreignDescriptorNameCacheOffset;
                std::string_view fullName(name);
                if ( protocolConformance.foreignMetadataNameHasImportInfo )
                    fullName = getForeignFullIdentity(name);

                // We only have 16-bits for the length.  Hopefully that is enough!
                if ( fullName.size() >= (1 << 16) ) {
                    diags.error("Protocol conformance exceeded name length of 16-bits");
                    stopProtocolConformance = true;
                    return;
                }

                SwiftForeignTypeProtocolConformanceLocation protoLoc;
                protoLoc.protocolConformanceCacheOffset = binaryCacheOffset + protocolConformanceRuntimeOffset;
                protoLoc.dylibObjCIndex = dylibObjCIndex;
                protoLoc.foreignDescriptorNameCacheOffset = (fullName.data() - (const char*)dyldCache);
                protoLoc.foreignDescriptorNameLength = fullName.size();
                protoLoc.protocolCacheOffset = binaryCacheOffset + protocolConformance.protocolRuntimeOffset;
                foundForeignTypeProtocolConformances.push_back(protoLoc);
                if ( log ) {
                    const char* typeName = "";
                    const char* protocolName = "";
                    const char* conformanceName = "";
                    typeName = (const char*)dyldCache + protoLoc.foreignDescriptorNameCacheOffset;
                    if ( auto it = symbols.find(protocolConformance.protocolRuntimeOffset); it != symbols.end() )
                        protocolName = it->second;
                    if ( auto it = symbols.find(protocolConformanceRuntimeOffset); it != symbols.end() )
                        conformanceName = it->second;
                    fprintf(stderr, "%s: (%s, %s) -> %s", ma->installName(), typeName, protocolName, conformanceName);
                }
            }
        });
    });

    return !diags.hasError();
}

static bool operator<(const SwiftTypeProtocolConformanceLocation& a,
                      const SwiftTypeProtocolConformanceLocation& b) {
    if ( a.typeDescriptorCacheOffset != b.typeDescriptorCacheOffset )
        return a.typeDescriptorCacheOffset < b.typeDescriptorCacheOffset;
    if ( a.protocolCacheOffset != b.protocolCacheOffset )
        return a.protocolCacheOffset < b.protocolCacheOffset;
    if ( a.raw != b.raw )
        return a.raw < b.raw;
    return false;
}

static bool operator<(const SwiftMetadataProtocolConformanceLocation& a,
                      const SwiftMetadataProtocolConformanceLocation& b) {
    if ( a.metadataCacheOffset != b.metadataCacheOffset )
        return a.metadataCacheOffset < b.metadataCacheOffset;
    if ( a.protocolCacheOffset != b.protocolCacheOffset )
        return a.protocolCacheOffset < b.protocolCacheOffset;
    if ( a.raw != b.raw )
        return a.raw < b.raw;
    return false;
}

static bool operator<(const SwiftForeignTypeProtocolConformanceLocation& a,
                      const SwiftForeignTypeProtocolConformanceLocation& b) {
    if ( a.foreignDescriptorNameCacheOffset != b.foreignDescriptorNameCacheOffset )
        return a.foreignDescriptorNameCacheOffset < b.foreignDescriptorNameCacheOffset;
    if ( a.foreignDescriptorNameLength != b.foreignDescriptorNameLength )
        return a.foreignDescriptorNameLength < b.foreignDescriptorNameLength;
    if ( a.protocolCacheOffset != b.protocolCacheOffset )
        return a.protocolCacheOffset < b.protocolCacheOffset;
    if ( a.raw != b.raw )
        return a.raw < b.raw;
    return false;
}

static void optimizeProtocolConformances(Diagnostics& diags, DyldSharedCache* dyldCache,
                                         uint8_t* swiftReadOnlyBuffer, uint64_t swiftReadOnlyBufferSizeAllocated)
{
    std::vector<SwiftTypeProtocolConformanceLocation> foundTypeProtocolConformances;
    std::vector<SwiftMetadataProtocolConformanceLocation> foundMetadataProtocolConformances;
    std::vector<SwiftForeignTypeProtocolConformanceLocation> foundForeignTypeProtocolConformances;
    if ( !findProtocolConformances(diags, dyldCache, foundTypeProtocolConformances, foundMetadataProtocolConformances,
                                   foundForeignTypeProtocolConformances) )
        return;

    // Sort the lists, and look for duplicates

    // Types
    std::sort(foundTypeProtocolConformances.begin(), foundTypeProtocolConformances.end());
    for (uint64_t i = 1; i < foundTypeProtocolConformances.size(); ++i) {
        // Check if this protocol is the same as the previous one
        auto& prev = foundTypeProtocolConformances[i - 1];
        auto& current = foundTypeProtocolConformances[i];
        if ( std::equal_to<SwiftTypeProtocolConformanceLocationKey>()(prev, current) )
            prev.nextIsDuplicate = 1;
    }

    std::vector<SwiftTypeProtocolConformanceLocationKey> typeProtocolConformanceKeys;
    for (const auto& protoLoc : foundTypeProtocolConformances) {
        if ( protoLoc.nextIsDuplicate )
            continue;
        typeProtocolConformanceKeys.push_back(protoLoc);
    }

    // Metadata
    std::sort(foundMetadataProtocolConformances.begin(), foundMetadataProtocolConformances.end());
    for (uint64_t i = 1; i < foundMetadataProtocolConformances.size(); ++i) {
        // Check if this protocol is the same as the previous one
        auto& prev = foundMetadataProtocolConformances[i - 1];
        auto& current = foundMetadataProtocolConformances[i];
        if ( std::equal_to<SwiftMetadataProtocolConformanceLocationKey>()(prev, current) )
            prev.nextIsDuplicate = 1;
    }

    std::vector<SwiftMetadataProtocolConformanceLocationKey> metadataProtocolConformanceKeys;
    for (const auto& protoLoc : foundMetadataProtocolConformances) {
        if ( protoLoc.nextIsDuplicate )
            continue;
        metadataProtocolConformanceKeys.push_back(protoLoc);
    }

    // Foreign types
    // First unique the offsets so that they all have the same offset for the same name
    {
        std::unordered_map<std::string_view, uint64_t> canonicalForeignNameOffsets;
        for (auto& protoLoc : foundForeignTypeProtocolConformances) {
            uint64_t nameOffset = protoLoc.foreignDescriptorNameCacheOffset;
            const char* name = (const char*)dyldCache + nameOffset;
            // The name might have additional ImportInfo, which may include null characters.
            // The size we calculated earlier includes any necessary null characters
            std::string_view fullName(name, protoLoc.foreignDescriptorNameLength);
            auto itAndInserted = canonicalForeignNameOffsets.insert({ fullName, nameOffset });
            if ( !itAndInserted.second ) {
                // We didn't insert the name, so use the offset already there for this name
                protoLoc.foreignDescriptorNameCacheOffset = itAndInserted.first->second;
            }
        }
    }

    std::sort(foundForeignTypeProtocolConformances.begin(), foundForeignTypeProtocolConformances.end());
    for (uint64_t i = 1; i < foundForeignTypeProtocolConformances.size(); ++i) {
        // Check if this protocol is the same as the previous one
        auto& prev = foundForeignTypeProtocolConformances[i - 1];
        auto& current = foundForeignTypeProtocolConformances[i];
        if ( std::equal_to<SwiftForeignTypeProtocolConformanceLocationKey>()(prev, current) )
            prev.nextIsDuplicate = 1;
    }

    std::vector<SwiftForeignTypeProtocolConformanceLocationKey> foreignTypeProtocolConformanceKeys;
    for (const auto& protoLoc : foundForeignTypeProtocolConformances) {
        if ( protoLoc.nextIsDuplicate )
            continue;
        foreignTypeProtocolConformanceKeys.push_back(protoLoc);
    }

    // Build a map of all found conformances

    // Build the perfect hash table for type conformances
    objc::PerfectHash typeConformancePerfectHash;
    make_perfect(typeProtocolConformanceKeys, nullptr, typeConformancePerfectHash);

    // Build the perfect hash table for metadata
    objc::PerfectHash metadataConformancePerfectHash;
    make_perfect(metadataProtocolConformanceKeys, nullptr, metadataConformancePerfectHash);

    // Build the perfect hash table for foreign types
    objc::PerfectHash foreignTypeConformancePerfectHash;
    make_perfect(foreignTypeProtocolConformanceKeys, (const uint8_t*)dyldCache, foreignTypeConformancePerfectHash);

    // Make space for all the hash tables
    uint8_t* bufferStart = swiftReadOnlyBuffer;
    uint8_t* bufferEnd = swiftReadOnlyBuffer + swiftReadOnlyBufferSizeAllocated;

    // Add a header
    SwiftOptimizationHeader* swiftOptimizationHeader = (SwiftOptimizationHeader*)swiftReadOnlyBuffer;
    swiftReadOnlyBuffer += sizeof(SwiftOptimizationHeader);

    // Make space for the type conformance map
    uint8_t* typeConformanceHashTableBuffer = swiftReadOnlyBuffer;
    size_t typeConformanceHashTableSize = SwiftHashTable::size(typeConformancePerfectHash);
    swiftReadOnlyBuffer += typeConformanceHashTableSize;

    // Make space for the metadata conformance map
    uint8_t* metadataConformanceHashTableBuffer = swiftReadOnlyBuffer;
    size_t metadataConformanceHashTableSize = SwiftHashTable::size(metadataConformancePerfectHash);
    swiftReadOnlyBuffer += metadataConformanceHashTableSize;

    // Make space for the foreign types conformance map
    uint8_t* foreignTypeConformanceHashTableBuffer = swiftReadOnlyBuffer;
    size_t foreignTypeConformanceHashTableSize = SwiftHashTable::size(foreignTypeConformancePerfectHash);
    swiftReadOnlyBuffer += foreignTypeConformanceHashTableSize;

    // Make space for the type conformance structs
    uint8_t* typeConformanceBuffer = swiftReadOnlyBuffer;
    size_t typeConformanceBufferSize = (foundTypeProtocolConformances.size() * sizeof(*foundTypeProtocolConformances.data()));
    swiftReadOnlyBuffer += typeConformanceBufferSize;

    // Make space for the metadata conformance structs
    uint8_t* metadataConformanceBuffer = swiftReadOnlyBuffer;
    size_t metadataConformanceBufferSize = (foundMetadataProtocolConformances.size() * sizeof(*foundMetadataProtocolConformances.data()));
    swiftReadOnlyBuffer += metadataConformanceBufferSize;

    // Make space for the foreign type conformance structs
    uint8_t* foreignTypeConformanceBuffer = swiftReadOnlyBuffer;
    size_t foreignTypeConformanceBufferSize = (foundForeignTypeProtocolConformances.size() * sizeof(*foundForeignTypeProtocolConformances.data()));
    swiftReadOnlyBuffer += foreignTypeConformanceBufferSize;

    // Check for overflow
    if ( swiftReadOnlyBuffer > bufferEnd ) {
        diags.error("Overflow in Swift type hash tables (%lld allocated vs %lld used",
                    swiftReadOnlyBufferSizeAllocated, (uint64_t)(swiftReadOnlyBuffer - bufferStart));
        return;
    }

    // Write all the hash tables
    dyldCache->header.swiftOptsOffset = (uint64_t)swiftOptimizationHeader - (uint64_t)dyldCache;
    dyldCache->header.swiftOptsSize = (uint64_t)swiftReadOnlyBuffer - (uint64_t)bufferStart;

    swiftOptimizationHeader->version = 1;
    swiftOptimizationHeader->padding = 0;
    swiftOptimizationHeader->typeConformanceHashTableCacheOffset = (uint64_t)typeConformanceHashTableBuffer - (uint64_t)dyldCache;
    swiftOptimizationHeader->metadataConformanceHashTableCacheOffset = (uint64_t)metadataConformanceHashTableBuffer - (uint64_t)dyldCache;
    swiftOptimizationHeader->foreignTypeConformanceHashTableCacheOffset = (uint64_t)foreignTypeConformanceHashTableBuffer - (uint64_t)dyldCache;

    ((SwiftHashTable*)typeConformanceHashTableBuffer)->write(typeConformancePerfectHash, foundTypeProtocolConformances,
                                                             typeConformanceBuffer, nullptr);
    ((SwiftHashTable*)metadataConformanceHashTableBuffer)->write(metadataConformancePerfectHash, foundMetadataProtocolConformances,
                                                                 metadataConformanceBuffer, nullptr);
    ((SwiftHashTable*)foreignTypeConformanceHashTableBuffer)->write(foreignTypeConformancePerfectHash, foundForeignTypeProtocolConformances,
                                                                    foreignTypeConformanceBuffer, (const uint8_t*)dyldCache);
    memcpy(typeConformanceBuffer, foundTypeProtocolConformances.data(), typeConformanceBufferSize);
    memcpy(metadataConformanceBuffer, foundMetadataProtocolConformances.data(), metadataConformanceBufferSize);
    memcpy(foreignTypeConformanceBuffer, foundForeignTypeProtocolConformances.data(), foreignTypeConformanceBufferSize);

    // Check that the hash tables work!
    for (const auto& target : foundTypeProtocolConformances) {
        const SwiftHashTable* hashTable = (const SwiftHashTable*)typeConformanceHashTableBuffer;
        const auto* protocolTarget = hashTable->getValue<SwiftTypeProtocolConformanceLocation>(target, nullptr);
        assert(protocolTarget != nullptr);
        if ( !protocolTarget->nextIsDuplicate ) {
            // No duplicates, so we should match
            assert(memcmp(protocolTarget, &target, sizeof(SwiftTypeProtocolConformanceLocation)) == 0);
        } else {
            // One of the duplicates should match
            bool foundMatch = false;
            while ( true ) {
                if ( memcmp(protocolTarget, &target, sizeof(SwiftTypeProtocolConformanceLocation)) == 0 ) {
                    foundMatch = true;
                    break;
                }
                if ( !protocolTarget->nextIsDuplicate )
                    break;
                protocolTarget = ++protocolTarget;
            }
            assert(foundMatch);
        }
    }
    for (const auto& target : foundMetadataProtocolConformances) {
        const SwiftHashTable* hashTable = (const SwiftHashTable*)metadataConformanceHashTableBuffer;
        const auto* protocolTarget = hashTable->getValue<SwiftMetadataProtocolConformanceLocation>(target, nullptr);
        assert(protocolTarget != nullptr);
        if ( !protocolTarget->nextIsDuplicate ) {
            // No duplicates, so we should match
            assert(memcmp(protocolTarget, &target, sizeof(SwiftMetadataProtocolConformanceLocation)) == 0);
        } else {
            // One of the duplicates should match
            bool foundMatch = false;
            while ( true ) {
                if ( memcmp(protocolTarget, &target, sizeof(SwiftMetadataProtocolConformanceLocation)) == 0 ) {
                    foundMatch = true;
                    break;
                }
                if ( !protocolTarget->nextIsDuplicate )
                    break;
                protocolTarget = ++protocolTarget;
            }
            assert(foundMatch);
        }
    }
    for (const auto& target : foundForeignTypeProtocolConformances) {
        const SwiftHashTable* hashTable = (const SwiftHashTable*)foreignTypeConformanceHashTableBuffer;
        const auto* protocolTarget = hashTable->getValue<SwiftForeignTypeProtocolConformanceLocation>(target, (const uint8_t*)dyldCache);
        assert(protocolTarget != nullptr);
        if ( !protocolTarget->nextIsDuplicate ) {
            // No duplicates, so we should match
            assert(memcmp(protocolTarget, &target, sizeof(SwiftForeignTypeProtocolConformanceLocation)) == 0);
        } else {
            // One of the duplicates should match
            bool foundMatch = false;
            while ( true ) {
                if ( memcmp(protocolTarget, &target, sizeof(SwiftForeignTypeProtocolConformanceLocation)) == 0 ) {
                    foundMatch = true;
                    break;
                }
                if ( !protocolTarget->nextIsDuplicate )
                    break;
                protocolTarget = ++protocolTarget;
            }
            assert(foundMatch);
        }
    }
    // Check the foreign table again, with a string key, as that is what the SPI will use
    for (const auto& target : foundForeignTypeProtocolConformances) {
        const SwiftHashTable* hashTable = (const SwiftHashTable*)foreignTypeConformanceHashTableBuffer;

        const char* typeName = (const char*)dyldCache + target.foreignDescriptorNameCacheOffset;
        assert((const uint8_t*)typeName == target.key1Buffer((const uint8_t*)dyldCache));
        // The type name might include null characters, if it has additional import info
        std::string_view fullName(typeName, target.key1Size());
        SwiftForeignTypeProtocolConformanceLookupKey lookupKey = { fullName, target.protocolCacheOffset };

        const auto* protocolTarget = hashTable->getValue<SwiftForeignTypeProtocolConformanceLookupKey, SwiftForeignTypeProtocolConformanceLocation>(lookupKey, (const uint8_t*)dyldCache);
        assert(protocolTarget != nullptr);
        if ( !protocolTarget->nextIsDuplicate ) {
            // No duplicates, so we should match
            assert(memcmp(protocolTarget, &target, sizeof(SwiftForeignTypeProtocolConformanceLocation)) == 0);
        } else {
            // One of the duplicates should match
            bool foundMatch = false;
            while ( true ) {
                if ( memcmp(protocolTarget, &target, sizeof(SwiftForeignTypeProtocolConformanceLocation)) == 0 ) {
                    foundMatch = true;
                    break;
                }
                if ( !protocolTarget->nextIsDuplicate )
                    break;
                protocolTarget = ++protocolTarget;
            }
            assert(foundMatch);
        }
    }

    diags.verbose("[Swift]: Wrote %lld bytes of hash tables\n", (uint64_t)(swiftReadOnlyBuffer - bufferStart));
}

void SharedCacheBuilder::optimizeSwift()
{
    DyldSharedCache* dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;

    // The only thing we do for now is optimize protocols conformances.  But we'll put that in
    // its own method just to keep it self-contained
    optimizeProtocolConformances(_diagnostics, dyldCache, _swiftReadOnlyBuffer, _swiftReadOnlyBufferSizeAllocated);
}

static uint32_t hashTableSize(uint32_t maxElements, uint32_t perElementData)
{
    uint32_t elementsWithPadding = maxElements*11/10; // if close to power of 2, perfect hash may fail, so don't get within 10% of that
    uint32_t powTwoCapacity = 1 << (32 - __builtin_clz(elementsWithPadding - 1));
    uint32_t headerSize = 4*(8+256);
    return headerSize + powTwoCapacity/2 + powTwoCapacity + powTwoCapacity*perElementData;
}

// Allocate enough space for the Swift hash tables in the read-only region of the cache
uint32_t SharedCacheBuilder::computeReadOnlySwift()
{
    __block uint32_t numTypeConformances = 0;
    __block uint32_t numMetadataConformances = 0;
    __block uint32_t numForeignMetadataConformances = 0;
    for (DylibInfo& dylib : _sortedDylibs) {
        Diagnostics diags;
        dylib.input->mappedFile.mh->forEachSwiftProtocolConformance(diags, dylib.input->mappedFile.mh->makeVMAddrConverter(false), false,
                                                                    ^(uint64_t protocolConformanceRuntimeOffset,
                                                                      const SwiftProtocolConformance &protocolConformance,
                                                                      bool &stopProtocolConformance) {
            if ( protocolConformance.protocolRuntimeOffset != 0 )
                ++numTypeConformances;
            else
                ++numMetadataConformances;

            if ( protocolConformance.foreignMetadataNameRuntimeOffset != 0 )
                ++numForeignMetadataConformances;
        });
    }
    // Each conformance entry is 3 uint64_t's internally, plus the space for the hash table
    uint32_t sizeNeeded = 0x4000 * 3;
    sizeNeeded += (numTypeConformances * 3 * sizeof(uint64_t)) + hashTableSize(numTypeConformances, 5);;
    sizeNeeded += (numMetadataConformances * 3 * sizeof(uint64_t)) + hashTableSize(numMetadataConformances, 5);
    sizeNeeded += (numForeignMetadataConformances * 3 * sizeof(uint64_t)) + hashTableSize(numForeignMetadataConformances, 5);
    return sizeNeeded;
}
#endif
