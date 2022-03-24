/*
* Copyright (c) 2020 Apple Inc. All rights reserved.
*
* @APPLE_LICENSE_HEADER_START@
*
* "Portions Copyright (c) 1999 Apple Computer, Inc.  All Rights
* Reserved.  This file contains Original Code and/or Modifications of
* Original Code as defined in and that are subject to the Apple Public
* Source License Version 1.0 (the 'License').  You may not use this file
* except in compliance with the License.  Please obtain a copy of the
* License at http://www.apple.com/publicsource and read it before using
* this file.
*
* The Original Code and all software distributed under the License are
* distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
* EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
* INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
* License for the specific language governing rights and limitations
* under the License."
*
* @APPLE_LICENSE_HEADER_END@
*/

//FIXME: This entire file can be deleted once we move to C++20 and get <bit>

#ifndef BitUtils_h
#define BitUtils_h

#if __x86_64__
#include <x86intrin.h>
#elif __arm64__
#include <arm_acle.h>
#endif

namespace {
// We can't directly use  __builtin_clzll because it is undefined for the input of zero. In practice it compiles to instructsions
// that are defined for inputs of zero (and does what we want) on all arm CPUs, but becuase it is UB the compiler may make optimize
// it incorrectly. On x86 we still support pre-Haswell, so it may compile to bsr which has the the wrong smeantics, so we use a
// software implementatio to generate the correct instructins.
#if __arm64__ || __arm__
__attribute__((no_sanitize("undefined")))
inline
uint8_t clz64(uint64_t value) {
    return __builtin_clzll(value);
}

inline
uint8_t ctz64(uint64_t value) {
    return 64 - clz64(~value & (value - 1));
}
#else
inline
uint8_t clz64(uint64_t value) {
    value = value | ( value >> 1);
    value = value | ( value >> 2);
    value = value | ( value >> 4);
    value = value | ( value >> 8);
    value = value | ( value >> 16);
    value = value | ( value >> 32);

    return __builtin_popcountll(~value);
}

inline
uint8_t ctz64(uint64_t value) {
    return __builtin_popcountll(~value & (value -1));
}
#endif

inline
uint8_t popcount(uint64_t value) {
    return __builtin_popcountll(value);
}

inline
uint64_t bit_ceil(uint64_t value) {
    value--;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;
    value++;
    return value;
}

}

#endif /* BitUtils_h */
