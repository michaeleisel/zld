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


#ifndef OptimizerSwift_h
#define OptimizerSwift_h

#include "Array.h"

struct SwiftOptimizationHeader {
    uint32_t version;
    uint32_t padding;
    uint64_t typeConformanceHashTableCacheOffset;
    uint64_t metadataConformanceHashTableCacheOffset;
    uint64_t foreignTypeConformanceHashTableCacheOffset;
};

// This is the key to the map from (type descriptor, protocol) to value
struct SwiftTypeProtocolConformanceLocationKey
{
    uint64_t typeDescriptorCacheOffset  = 0;
    uint64_t protocolCacheOffset        = 0;

    // To make it easier to hash different sized structs in the same algorithm,
    // we pass each value individually to the perfect hash
    const uint8_t* key1Buffer(const uint8_t* stringBaseAddress) const {
        return (const uint8_t*)&typeDescriptorCacheOffset;
    }

    const uint32_t key1Size() const {
        return sizeof(typeDescriptorCacheOffset);
    }

    const uint8_t* key2Buffer(const uint8_t* stringBaseAddress) const {
        return (const uint8_t*)&protocolCacheOffset;
    }

    const uint32_t key2Size() const {
        return sizeof(protocolCacheOffset);
    }
};

// The start of this struct, the SwiftTypeProtocolConformanceLocationKey, is the key
// to the map, while this whole struct is the value too
struct SwiftTypeProtocolConformanceLocation : SwiftTypeProtocolConformanceLocationKey
{
    typedef SwiftTypeProtocolConformanceLocationKey KeyType;

    union {
        uint64_t raw = 0;
        struct {
            uint64_t nextIsDuplicate                : 1,    // == 0
                     protocolConformanceCacheOffset : 47,   // Offset from the shared cache base to the conformance object
                     dylibObjCIndex                 : 16;   // Index in to the HeaderInfoRW dylibs for the dylib containing this conformance
        };
    };
};

// This is the key to the map from (metadata, protocol) to value
struct SwiftMetadataProtocolConformanceLocationKey
{
    uint64_t metadataCacheOffset = 0;
    uint64_t protocolCacheOffset = 0;

    // To make it easier to hash different sized structs in the same algorithm,
    // we pass each value individually to the perfect hash
    const uint8_t* key1Buffer(const uint8_t* stringBaseAddress) const {
        return (const uint8_t*)&metadataCacheOffset;
    }

    const uint32_t key1Size() const {
        return sizeof(metadataCacheOffset);
    }

    const uint8_t* key2Buffer(const uint8_t* stringBaseAddress) const {
        return (const uint8_t*)&protocolCacheOffset;
    }

    const uint32_t key2Size() const {
        return sizeof(protocolCacheOffset);
    }
};

// The start of this struct, the SwiftMetadataProtocolConformanceLocationKey, is the key
// to the map, while this whole struct is the value too
struct SwiftMetadataProtocolConformanceLocation : SwiftMetadataProtocolConformanceLocationKey
{
    typedef SwiftMetadataProtocolConformanceLocationKey KeyType;

    union {
        uint64_t raw = 0;
        struct {
            uint64_t nextIsDuplicate                : 1,    // == 0
                     protocolConformanceCacheOffset : 47,   // Offset from the shared cache base to the conformance object
                     dylibObjCIndex                 : 16;   // Index in to the HeaderInfoRW dylibs for the dylib containing this conformance
        };
    };
};

// This is the key to the map from (foreign type name, protocol) to value
struct SwiftForeignTypeProtocolConformanceLocationKey
{
    union {
        uint64_t rawForeignDescriptor = 0;
        struct {
            uint64_t foreignDescriptorNameCacheOffset : 48,
                     foreignDescriptorNameLength      : 16;
        };
    };
    uint64_t protocolCacheOffset                = 0;

    // To make it easier to hash different sized structs in the same algorithm,
    // we pass each value individually to the perfect hash
    const uint8_t* key1Buffer(const uint8_t* stringBaseAddress) const {
        return (const uint8_t*)(stringBaseAddress + foreignDescriptorNameCacheOffset);
    }

    const uint32_t key1Size() const {
        return (uint32_t)foreignDescriptorNameLength;
    }

    const uint8_t* key2Buffer(const uint8_t* stringBaseAddress) const {
        return (const uint8_t*)&protocolCacheOffset;
    }

    const uint32_t key2Size() const {
        return sizeof(protocolCacheOffset);
    }
};

static_assert(sizeof(SwiftForeignTypeProtocolConformanceLocationKey) == 16);

// The start of this struct, the SwiftTypeProtocolConformanceLocationKey, is the key
// to the map, while this whole struct is the value too
struct SwiftForeignTypeProtocolConformanceLocation : SwiftForeignTypeProtocolConformanceLocationKey
{
    typedef SwiftForeignTypeProtocolConformanceLocationKey KeyType;

    union {
        uint64_t raw = 0;
        struct {
            uint64_t nextIsDuplicate                : 1,    // == 0
                     protocolConformanceCacheOffset : 47,   // Offset from the shared cache base to the conformance object
                     dylibObjCIndex                 : 16;   // Index in to the HeaderInfoRW dylibs for the dylib containing this conformance
        };
    };
};

// At runtime, we lookup foreign types with a string instead of an offset.  This is the key which does that lookup
struct SwiftForeignTypeProtocolConformanceLookupKey
{
    std::string_view foreignDescriptorName;
    uint64_t protocolCacheOffset        = 0;
};

class VIS_HIDDEN SwiftHashTable {
public:
    // As target values are written immediately after this hash table, a uint32_t offset can reach them
    typedef uint32_t TargetOffsetType;

private:
    typedef uint8_t CheckByteType;

protected:

    uint32_t capacity;
    uint32_t occupied;
    uint32_t shift;
    uint32_t mask;
    TargetOffsetType sentinelTarget;
    uint32_t roundedTabSize;
    uint64_t salt;

    uint32_t scramble[256];
    uint8_t tab[0];                   /* tab[mask+1] (always power-of-2). Rounded up to roundedTabSize */
    // uint8_t checkbytes[capacity];  /* check byte for each string */
    // int32_t offsets[capacity];     /* offsets from &capacity to cstrings */

    CheckByteType* checkBytesOffset() {
        return &tab[roundedTabSize];
    }

    const CheckByteType* checkBytesOffset() const {
        return &tab[roundedTabSize];
    }

    TargetOffsetType* targetsOffset() {
        return (TargetOffsetType*)(checkBytesOffset() + capacity);
    }

    const TargetOffsetType* targetsOffset() const {
        return (TargetOffsetType*)(checkBytesOffset() + capacity);
    }

    dyld3::Array<CheckByteType> checkBytes() {
        return dyld3::Array<CheckByteType>((CheckByteType *)checkBytesOffset(), capacity, capacity);
    }
    const dyld3::Array<CheckByteType> checkBytes() const {
        return dyld3::Array<CheckByteType>((CheckByteType *)checkBytesOffset(), capacity, capacity);
    }

    dyld3::Array<TargetOffsetType> targets() {
        return dyld3::Array<TargetOffsetType>((TargetOffsetType *)targetsOffset(), capacity, capacity);
    }
    const dyld3::Array<TargetOffsetType> targets() const {
        return dyld3::Array<TargetOffsetType>((TargetOffsetType *)targetsOffset(), capacity, capacity);
    }

    template<typename TargetT>
    uint32_t hash(const TargetT& key, const uint8_t* stringBaseAddress) const;

    template<typename ValueT, typename MapEntryT>
    bool equal(const MapEntryT& key, const ValueT& value, const uint8_t* stringBaseAddress) const;

    // The check bytes are used to reject values that aren't in the table
    // without paging in the table's value data
    template<typename TargetT>
    CheckByteType checkbyte(const TargetT& key, const uint8_t* stringBaseAddress) const;

    template<typename ValueT, typename MapEntryT>
    TargetOffsetType getPotentialTarget(const ValueT& value, const uint8_t* stringBaseAddress) const
    {
        uint32_t index = getIndex<ValueT, MapEntryT>(value, stringBaseAddress);
        if (index == indexNotFound)
            return sentinelTarget;
        return targets()[index];
    }

    enum : uint32_t {
        indexNotFound = ~0U
    };

    enum : TargetOffsetType {
        sentinel = ~0U
    };

    template<typename ValueT, typename MapEntryT>
    uint32_t getIndex(const ValueT& value, const uint8_t* stringBaseAddress) const
    {
        uint32_t h = hash(value, stringBaseAddress);

        // Use check byte to reject without paging in the table's cstrings
        CheckByteType h_check = checkBytes()[h];
        CheckByteType key_check = checkbyte(value, stringBaseAddress);
        if (h_check != key_check)
            return indexNotFound;

        TargetOffsetType targetOffset = targets()[h];
        if (targetOffset == sentinel)
            return indexNotFound;
        const MapEntryT& key = *(const MapEntryT*)((const uint8_t*)this + targetOffset);
        if ( !equal(key, value, stringBaseAddress) )
            return indexNotFound;

        return h;
    }

public:

    template<typename PerfectHashT>
    static size_t size(const PerfectHashT& phash) {
        // Round tab[] to at least 4 in length to ensure the uint32_t's after are aligned
        uint32_t roundedTabSize = std::max(phash.mask+1, 4U);
        size_t tableSize = 0;
        tableSize += sizeof(SwiftHashTable);
        tableSize += roundedTabSize;
        tableSize += phash.capacity * sizeof(CheckByteType);
        tableSize += phash.capacity * sizeof(TargetOffsetType);
        return tableSize;
    }

    // Get a value if it has an entry in the table
    template<typename ValueT, typename MapEntryT>
    const MapEntryT* getValue(const ValueT& value, const uint8_t* stringBaseAddress) const {
        TargetOffsetType targetOffset = getPotentialTarget<ValueT, typename MapEntryT::KeyType>(value, stringBaseAddress);
        if ( targetOffset != sentinelTarget ) {
            return (const MapEntryT*)((const uint8_t*)this + targetOffset);
        }
        return nullptr;
    }

    // Get a value if it has an entry in the table
    template<typename MapEntryT>
    const MapEntryT* getValue(const typename MapEntryT::KeyType& value, const uint8_t* stringBaseAddress) const {
        TargetOffsetType targetOffset = getPotentialTarget<typename MapEntryT::KeyType, typename MapEntryT::KeyType>(value, stringBaseAddress);
        if ( targetOffset != sentinelTarget ) {
            return (const MapEntryT*)((const uint8_t*)this + targetOffset);
        }
        return nullptr;
    }

#if BUILDING_CACHE_BUILDER
    template<typename PerfectHashT, typename TargetT>
    void write(const PerfectHashT& phash, const std::vector<TargetT>& targets,
               const uint8_t* targetValuesBufferBaseAddress,
               const uint8_t* stringBaseAddress);
#endif

    template<typename TargetT>
    void forEachValue(void (^callback)(uint32_t bucketIndex,
                                       const dyld3::Array<TargetT>& impls)) const {
        for ( unsigned i = 0; i != capacity; ++i ) {
            TargetOffsetType targetOffset = targets()[i];
            if (targetOffset == sentinel) {
                callback(i, {});
            } else {
                const TargetT* bucketValue = (const TargetT*)((const uint8_t*)this + targetOffset);
                if ( !bucketValue->nextIsDuplicate ) {
                    // This value has a single implementation
                    const dyld3::Array<TargetT> implTarget((TargetT*)bucketValue, 1, 1);
                    callback(i, implTarget);
                } else {
                    uint32_t numEntries = 1;
                    const TargetT* currentValue = bucketValue;
                    while ( currentValue->nextIsDuplicate ) {
                        ++numEntries;
                        ++currentValue;
                    }
                    const dyld3::Array<TargetT> implTarget((TargetT*)bucketValue, numEntries, numEntries);
                    callback(i, implTarget);
                }
            }
        }
    }

};

#endif /* OptimizerSwift_h */
