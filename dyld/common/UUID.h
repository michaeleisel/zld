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
#ifndef UUID_h
#define UUID_h

#include <array>
#include <string>
#include <cstdint>
#include <functional>
#include <string_view>

namespace DRL {
struct UUID {
    UUID() {}
    UUID(const uint8_t* uuid) : _data({0}) {
        std::copy(&uuid[0], &uuid[16], &_data[0]);
    }
    bool operator==(const UUID& other) const {
        return std::equal(_data.begin(), _data.end(), other._data.begin(), other._data.end());
    }
    bool operator!=(const UUID& other) const {
        return !(other == *this);
    }
    bool operator<(const UUID& other) const {
        return std::lexicographical_compare(_data.begin(), _data.end(), other._data.begin(), other._data.end());
    }

    explicit operator bool() {
        return std::any_of(_data.begin(), _data.end(), [](const uint8_t& a){
            return a != 0;
        });
    }
#if 0 // Debugging
    std::string str() const {
        char buffer[64];
        const uint8_t* uu = (const uint8_t*)&_data[0];
        sprintf(&buffer[0], "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", uu[0], uu[1], uu[2], uu[3],
                uu[4], uu[5], uu[6], uu[7], uu[8], uu[9], uu[10], uu[11], uu[12], uu[13], uu[14], uu[15]);
        return buffer;
    }
#endif
    bool empty() const {
        for (auto i = 0; i < 16; ++i) {
            if (_data[i] != 0) {
                return false;
            }
        }
        return true;
    }
    uint8_t* begin() { return &_data[0]; }
    uint8_t* end() { return &_data[16]; }
    const uint8_t* begin() const { return &_data[0]; }
    const uint8_t* end() const { return &_data[15]; }
    const uint8_t* cbegin() const { return &_data[0]; }
    const uint8_t* cend() const { return &_data[15]; }
private:
    friend std::hash<DRL::UUID>;
    std::array<uint8_t, 16> _data = {0};
};
};

namespace std {
    template<> struct hash<DRL::UUID>
    {
        std::size_t operator()(DRL::UUID const& U) const noexcept {
            size_t result = 0;
            for (auto i = 0; i < 16/sizeof(size_t); ++i) {
                size_t fragment = *((size_t *)&U._data[i*sizeof(size_t)]);
                result ^= fragment;
            }
            return result;
        }
    };
};

#endif /* UUID_h */
