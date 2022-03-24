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

#ifndef BumpAllocator_h
#define BumpAllocator_h

#include <unistd.h>
#include <stdio.h>

namespace dyld4 {

class BumpAllocator
{
public:
                BumpAllocator() { }
                ~BumpAllocator();

    void        append(const void* payload, size_t payloadSize);
    void        zeroFill(size_t payloadSize);
    void        align(unsigned multipleOf);
    size_t      size() const { return _usageEnd - _vmAllocationStart; }
    const void* finalize();

private:
    template <typename T>
    friend class BumpAllocatorPtr;
    uint8_t*    start() { return _vmAllocationStart; }

protected:
    uint8_t* _vmAllocationStart = nullptr;
    size_t   _vmAllocationSize  = 0;
    uint8_t* _usageEnd          = nullptr;
};

// Gives a safe pointer in to a BumpAllocator.  This pointer is safe to use across
// appends to the allocator which might change the address of the allocated memory.
template <typename T>
class BumpAllocatorPtr
{
public:
    BumpAllocatorPtr(BumpAllocator& allocator, uintptr_t offset)
        : _allocator(allocator)
        , _offset(offset)
    {
    }

    T* get() const
    {
        return (T*)(_allocator.start() + _offset);
    }

    T* operator->() const
    {
        return get();
    }

private:
    BumpAllocator& _allocator;
    uintptr_t      _offset;
};

} // namespace dyld4

#endif // BumpAllocator_h


