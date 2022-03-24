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

#ifndef PerfectHash_h
#define PerfectHash_h

#include <stdint.h>
#include <stdlib.h>

#include "Defines.h"

#include "Array.h"
#include "Map.h"

#if BUILDING_CACHE_BUILDER || BUILDING_UNIT_TESTS
#include <unordered_map>
#endif

namespace objc {

uint64_t lookup8(const uint8_t *k, size_t length, uint64_t level);

#if BUILDING_CACHE_BUILDER || BUILDING_UNIT_TESTS

struct eqstr {
    bool operator()(const char* s1, const char* s2) const {
        return strcmp(s1, s2) == 0;
    }
};

struct hashstr {
    size_t operator()(const char *s) const {
        return (size_t)objc::lookup8((uint8_t *)s, strlen(s), 0);
    }
};

// cstring => cstring's vmaddress
// (used for selector names and class names)
typedef std::unordered_map<const char *, uint64_t, hashstr, eqstr> string_map;

// cstring => cstring's vmaddress
// (used for selector names and class names)
typedef std::unordered_map<const char *, uint64_t, hashstr, eqstr> string_map;

// protocol name => protocol vmaddress
typedef std::unordered_map<const char *, uint64_t, hashstr, eqstr> legacy_protocol_map;

// protocol name => (protocol vmaddress, dylib objc index)
typedef std::unordered_multimap<const char *, std::pair<uint64_t, uint16_t>, hashstr, eqstr> protocol_map;

// class name => (class vmaddress, dylib objc index)
typedef std::unordered_multimap<const char *, std::pair<uint64_t, uint16_t>, hashstr, eqstr> class_map;

#endif // #if BUILDING_CACHE_BUILDER

struct VIS_HIDDEN PerfectHash {
    uint32_t capacity   = 0;
    uint32_t occupied   = 0;
    uint32_t shift      = 0;
    uint32_t mask       = 0;
    uint64_t salt       = 0;

    uint32_t scramble[256];
    dyld3::OverflowSafeArray<uint8_t> tab; // count == mask+1

    /* representation of a key */
    struct key
    {
#if BUILDING_CACHE_BUILDER || BUILDING_UNIT_TESTS
        uint8_t        *name1_k;        /* the actual key */
        uint32_t         len1_k;        /* the length of the actual key */
        uint8_t        *name2_k;        /* the actual key */
        uint32_t         len2_k;        /* the length of the actual key */
#else
        uint8_t        *name_k;         /* the actual key */
        uint32_t         len_k;         /* the length of the actual key */
#endif
        uint32_t         hash_k;        /* the initial hash value for this key */
        /* beyond this point is mapping-dependent */
        uint32_t         a_k;           /* a, of the key maps to (a,b) */
        uint32_t         b_k;           /* b, of the key maps to (a,b) */
        struct key *nextb_k;            /* next key with this b */
    };

    static void make_perfect(dyld3::OverflowSafeArray<key>& keys, PerfectHash& result);

    // For dyld at runtime to create perfect hash tables in the PrebuiltObjC
    static void make_perfect(const dyld3::OverflowSafeArray<const char*>& strings, objc::PerfectHash& phash);

#if BUILDING_CACHE_BUILDER || BUILDING_UNIT_TESTS
    // For the shared cache builder selector/class/protocol maps
    static void make_perfect(const string_map& strings, objc::PerfectHash& phash);
#endif
};

} // namespace objc

#endif /* PerfectHash_h */
