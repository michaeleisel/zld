//
//  Allocator.cpp
//  UnitTests
//
//  Created by Louis Gerbarg on 1/5/21.
//

#include <cstdio>
#include <algorithm>
#include <sys/mman.h>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <sanitizer/asan_interface.h>

#include "Defines.h"
#include "Allocator.h"
#include "BitUtils.h"

extern "C" void* __dso_handle;

namespace {
void* align(size_t alignment, size_t size, void*& ptr, size_t& space)
{
    void* r = nullptr;
    if (size <= space)
    {
        char* p1 = static_cast<char*>(ptr);
        char* p2 = reinterpret_cast<char*>(reinterpret_cast<size_t>(p1 + (alignment - 1)) & -alignment);
        size_t d = static_cast<size_t>(p2 - p1);
        if (d <= space - size)
        {
            r = p2;
            ptr = r;
            space -= d;
        }
    }
    return r;
}
}

namespace dyld4 {

#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#define ASAN_ENABLED (1)
#else
#define ASAN_ENABLED (0)
#endif

#pragma mark -
#pragma mark Constants and utility functions

size_t Allocator::roundToGranule(size_t size) {
    return ((size+(kGranuleSize-1)) & -kGranuleSize);
}

size_t Allocator::roundToPage(size_t size) {
    return ((size+(kPageSize-1)) & -kPageSize);
}

#pragma mark -
#pragma mark Common Utility functionality for allocators

bool Allocator::Buffer::operator==(const Buffer& O) const {
    return O.address == address && O.size == size;
}

bool Allocator::Buffer::operator<(const Buffer& O) const {
    return address < O.address ;
}

void* Allocator::Buffer::lastAddress() const {
    return (void*)((uintptr_t)address + size);
}

bool Allocator::Buffer::contains(const Buffer& region) const {
    if (region.address < address) { return false; }
    if (region.lastAddress() > lastAddress()) { return false; }
    return true;
}

bool Allocator::Buffer::valid() const {
    return (address != nullptr);
}

void Allocator::Buffer::dump() const {
    printf("\t%zu @ 0x%lx - 0x%lx\n", size, (uintptr_t)address, (uintptr_t)address+size);
}

#pragma mark -
#pragma mark Primitive allocator implementations

[[nodiscard]] Allocator::Buffer Allocator::vm_allocate_bytes(std::size_t size) {
    vm_address_t    result;
    _vmAllocatedBytes += size;

    kern_return_t kr = vm_allocate(mach_task_self(), &result, size, VM_FLAGS_ANYWHERE | VM_MAKE_TAG(VM_MEMORY_DYLD));
#if BUILDING_DYLD && TARGET_OS_OSX && __x86_64__
    // rdar://79214654 support wine games that need low mem.  Move dyld heap out of low mem
    if ( (kr == KERN_SUCCESS) && (result < 0x100000000ULL) ) {
        vm_address_t result2 = (long)&__dso_handle + 0x00200000; // look for vm range after dyld
        kern_return_t kr2 = vm_allocate(mach_task_self(), &result2, size, VM_FLAGS_FIXED | VM_MAKE_TAG(VM_MEMORY_DYLD));
        if ( kr2 == KERN_SUCCESS ) {
            (void)vm_deallocate(mach_task_self(), result, size);
            result = result2;
        }
    }
#endif

    if (kr != KERN_SUCCESS) {
        return {nullptr, 0};
    }
    return {(void*)result, size};
}

void Allocator::vm_deallocate_bytes(void* p, std::size_t size) {
    _vmAllocatedBytes -= size;
    (void)vm_deallocate(mach_task_self(), (vm_address_t)p, size);
}

#pragma mark -
#pragma mark FreeList Allocator

Allocator::~Allocator() {
    Allocator::Buffer regions[_regionCount];
    std::copy(&_regionList[0], &_regionList[_regionCount], &regions[0]);
    for (size_t i = 0; i < _regionCount; ++i) {
        vm_deallocate_bytes(regions[i].address, regions[i].size);
    }
    assert(_vmAllocatedBytes == 0);
}

Allocator& Allocator::operator=(Allocator&& O) {
    withLockedFreeList([&]{
        O.withLockedFreeList([&]{
            std::swap(_freeListHead, O._freeListHead);
            std::swap(_allocatedBytes, O._allocatedBytes);
            std::swap(_vmAllocatedBytes, O._vmAllocatedBytes);
            std::swap(_regionList, O._regionList);
            std::swap(_regionCount, O._regionCount);
        });
    });
    return *this;
}


void Allocator::dumpFreeList () {
    withLockedFreeList([&]{
        printf("Freelist:\n");
        for (auto current = &_freeListHead; current->valid(); current = current->next()) {
            current->dump();
        }
    });
}
void Allocator::dumpRegionList () {
    withLockedFreeList([&]{
        printf("Region List (%zu):\n", _regionCount);
        for (size_t i = 0; i < _regionCount; ++i) {
            printf("%lu:", i);
            _regionList[i].dump();
        }
    });
}


[[nodiscard]] Allocator::Buffer Allocator::allocate_buffer(std::size_t nbytes, std::size_t alignment) {
    return allocate_buffer(nbytes, alignment, false);
}

[[nodiscard]] Allocator::Buffer Allocator::allocate_buffer(std::size_t nbytes, std::size_t alignment, bool managed) {
    Buffer result;
    withLockedFreeList([&]{
        assert(popcount(alignment) == 1); //Power of 2
        assert(alignment <= kPageSize);
        const size_t targetAlignment = std::max<size_t>(16ULL, alignment);
        size_t targetSize = (std::max<size_t>(nbytes, 16ULL) + (targetAlignment-1)) & (-1*targetAlignment);
        uint8_t newRegionCount = 0;
        Buffer newRegions[2]; // The most we can ever need, if we need a new pool for the allocation, and another for the metadata
        Buffer* newRegionList = nullptr;
        if (nbytes > kPoolSize) {
            // This allocation is too large, send it directly to the upstream allocator
            result = vm_allocate_bytes(roundToPage(nbytes));
            newRegions[newRegionCount++] = result;
        } else {
            // Attempt to reserve space
            result = reserveSpace(targetSize, targetAlignment, managed, ASAN_ENABLED);
            if (result.address == nullptr) {
                // Not enough freespace found, allocate more
                auto pool = vm_allocate_bytes(kPoolSize);
                returnSpace(pool, false, false);
                newRegions[newRegionCount++] = pool;
                result = reserveSpace(targetSize, targetAlignment, managed, ASAN_ENABLED);
                assert(result.address != nullptr);
            }
        }
        if (newRegionCount) {
            // We allocated new regions (either for a pool or a large object), attempt to allocate space for the new _regionList
            auto newRegionListBuffer = reserveSpace(roundToGranule(sizeof(Buffer)*(_regionCount+newRegionCount)), kGranuleSize, false, false);
            if (newRegionListBuffer.address == nullptr) {
                // Allocation failed, ask the upstream allocator for a new pool
                auto pool = vm_allocate_bytes(kPoolSize);
                returnSpace(pool, false, false);
                newRegions[newRegionCount++] = pool;
                newRegionListBuffer = reserveSpace(roundToGranule(sizeof(Buffer)*(_regionCount+newRegionCount)), kGranuleSize, false, false);
            }
            newRegionList = (Buffer*)newRegionListBuffer.address;
            // Now merge the existing region list and the new regions into the newly allocated space
            std::merge(&_regionList[0], &_regionList[_regionCount], &newRegions[0], &newRegions[newRegionCount], newRegionList);
            // Return any space used by the existing _regionList and update the pointer/size
            if (_regionList) {
                returnSpace({_regionList, roundToGranule(sizeof(Buffer)*_regionCount) }, false, false);
            }
            _regionList = newRegionList;
            _regionCount += newRegionCount;
        }
        _allocatedBytes  += (result.size + (managed ? kGranuleSize : 0));
    });
    
//    if (managed) {
//        printf("Allocated %zu@%lx (aligned %lu)\n", result.size+kGranuleSize, (uintptr_t)result.address-kGranuleSize, alignment);
//    } else {
//        printf("Allocated %zu@%lx (aligned %lu)\n", result.size, (uintptr_t)result.address, alignment);
//    }
    return result;
}

void Allocator::removeRegion(const Allocator::Buffer &removedRegion) {
    if (removedRegion.address) {
        for (auto current = &_freeListHead; current->valid(); current = current->next()) {
            if (current->contains(removedRegion)) {
                current->isolateRegion(removedRegion);
                break;
            }
        }
        vm_deallocate_bytes((void*)removedRegion.address, removedRegion.size);
        auto i = std::lower_bound(&_regionList[0], &_regionList[_regionCount], removedRegion);
        assert(i != &_regionList[_regionCount]);
        std::copy(i+1, &_regionList[_regionCount], i);
        --_regionCount;
        if ((uintptr_t)&_regionList[_regionCount]%kGranuleSize == 0) {
            // The removed element was 16 byte aligned, so the granule is now free, return it
            returnSpace({(void*)&_regionList[_regionCount], 16ULL }, false, false);
        }
    }
}

void Allocator::deallocate_buffer(Buffer buffer) {
//    printf("Deallocated %zu@%lx\n", buffer.size, (uintptr_t)buffer.address);
    withLockedFreeList([&]{
        _allocatedBytes  -= buffer.size;
        if (buffer.size > kPoolSize) {
            // For large objects mark their entire region for removal
                removeRegion(buffer);
        } else {
            // For smaller allocations remove them, but then scan the _regionList to see if we have emptied any regions and mark
            // them for removal
            returnSpace(buffer, true, ASAN_ENABLED);
        }
    });
}

void Allocator::deallocate_bytes(void* p, std::size_t nbytes, std::size_t alignment) {
    const size_t targetAlignment = std::max<size_t>(16ULL, alignment);
    const size_t targetSize = (std::max<size_t>(nbytes, 16ULL) + (targetAlignment-1)) & (-1*targetAlignment);
    deallocate_buffer({p, targetSize});
};

// This searches through the free list to find a region with enough space, and then
// uses reserveRegion to reserve it
Allocator::Buffer Allocator::reserveSpace(std::size_t nbytes, std::size_t alignment, bool managed, bool guard) {
    if (guard) {
        nbytes += kGranuleSize;
    }

    Allocator::FreeListEntry* candidate = nullptr;
    size_t candidateScore = std::numeric_limits<size_t>::max();
    void* candidateAddress = nullptr;
    for (auto current = &_freeListHead; current->valid(); current = current->next()) {
        auto localAddress = current->address;
        auto localSize = current->size;
        if (managed) {
            // Save some space for a managed
            localAddress = (void*)((uintptr_t)localAddress + kGranuleSize);
            localSize -= kGranuleSize;

        }
        auto score = std::numeric_limits<size_t>::max();;
        if (align(alignment, nbytes, localAddress, localSize)) {
            score = (current->size - nbytes);
        }
        if (score < candidateScore) {
            candidateScore = score;
            candidate = current;
            candidateAddress = localAddress;
        }
        if (candidateScore == 0) { break; }
    }

    if (candidate == nullptr) { return {nullptr, 0}; }
    if (managed) {
        candidateAddress = (void*)((uintptr_t)candidateAddress - kGranuleSize);
        nbytes += kGranuleSize;
    }
    Buffer region = {candidateAddress, nbytes};
    candidate->isolateRegion(region);
    
    if (managed) {
        region.address = (void*)((uintptr_t)region.address + kGranuleSize);
        region.size -= kGranuleSize;
    }

    if (guard) {
        region.size -= kGranuleSize;
        ASAN_POISON_MEMORY_REGION(region.lastAddress(), kGranuleSize);
    }
    return region;
}

// This returns space to the free list. It is also used to add freshly allocated space to
// the free list. It will merge any adjacent free list entries
void Allocator::returnSpace(Buffer region, bool deallocate, bool guard) {
    if (guard) {
        ASAN_UNPOISON_MEMORY_REGION(region.lastAddress(), kGranuleSize);
        region.size += kGranuleSize;
    }
    ASAN_POISON_MEMORY_REGION((void*)((uintptr_t)region.address+sizeof(Buffer)), region.size-sizeof(Buffer));
    Allocator::FreeListEntry* i = &_freeListHead;
    Allocator::FreeListEntry* last = nullptr;
    while (i->valid()) {
        if (i->address > region.address) {
            break;
        }
        last = i;
        i = i->next();
    }
    auto temp = Allocator::FreeListEntry();
    if (i->valid()) {
        temp = *i;
    }
    *i = region;
    *i->next() = temp;
    i->mergeNext();
    if (last) {
        if (last->mergeNext()) {
            i = last;
        }
    }
    if (!deallocate) { return; }
    for (size_t j = 0; j < _regionCount; ++j) {
        if (i->contains(_regionList[j])) {
            removeRegion(_regionList[j]);
            // We want to keep scanning in case this allocation straddled a pool boundary. Lower the index since
            // removeRegion() shifts everything down by 1
            --j;
        }
    }
}

std::size_t Allocator::allocated_bytes() {
    // Skip locking as this is really only for debugging does not touch the freelist
    return _allocatedBytes;
}

void Allocator::validateFreeList() {
    withLockedFreeList([&]{
        auto current = &_freeListHead;
        auto last = (Allocator::FreeListEntry*)nullptr;
        while(current->valid()) {
            if (last) {
                assert(current->address > last->address);
                assert(current->address > last->lastAddress());
            }
            last = current;
            current = current->next();
        }
    });
}

#pragma mark -
#pragma mark FreeList::Entry implementation
// A free list entry is essentially a region. The one special property is that they form a linked list,
// so, if you dereference the address field you will get the next free list entry. This means that both
// the address AND the size of the a FreeListEntry are stored in the entry before it. This is a bit
// non-intuitive compared to storing the length of the current entry and a pointer to the next, but it
// it simplifies a lot of code.
Allocator::FreeListEntry::FreeListEntry(const Allocator::Buffer& R) : Buffer(R) {}

    // Returns the new (second) entry
Allocator::FreeListEntry* Allocator::FreeListEntry::split(size_t entrySize) {
    assert(address != nullptr);
    assert(size > entrySize);
    auto newEntry = (Allocator::FreeListEntry*)((uintptr_t)address+entrySize);
    ASAN_UNPOISON_MEMORY_REGION((void*)newEntry, sizeof(Buffer));
    *newEntry = *next();
    next()->address = newEntry;
    next()->size = size - entrySize;
    size = entrySize;
    assert(lastAddress() == next()->address);
    return (Allocator::FreeListEntry*)next();
}

bool Allocator::FreeListEntry::mergeNext() {
    if (next() == nullptr) { return false; }
    if (lastAddress() != next()->address) { return false; }
    auto asanTemp = lastAddress();
    size += next()->size;
    *next() = *next()->next();
    ASAN_UNPOISON_MEMORY_REGION(asanTemp, sizeof(Buffer));
    return true;
}

// This take a subregion of the entry, and fragments entry such that that the entry now exactly matches the bounds of the region, creating new adjacent free list entries if necessary.
void Allocator::FreeListEntry::isolateRegion(Allocator::Buffer region) {
    ASAN_UNPOISON_MEMORY_REGION((void*)((uintptr_t)region.address+sizeof(Buffer)), region.size-sizeof(Buffer));
    assert(address != nullptr);
    assert(address <= region.address);
    assert(region.lastAddress() <= lastAddress());
    auto activeFreeListEntry = this;
    if (activeFreeListEntry->address != region.address) {
        // Our region starts in the middle of the freespace, keep the existing free list entry but reduce the size
        activeFreeListEntry = split((uintptr_t)region.address - (uintptr_t)address);
    }
    if (activeFreeListEntry->size != region.size) {
        // Our region has extra space at the end, split it off
        activeFreeListEntry->split(region.size);
    }
    *activeFreeListEntry = *activeFreeListEntry->next();
}

Allocator::FreeListEntry* Allocator::FreeListEntry::next() const {
    return (Allocator::FreeListEntry*)address;
}

void Allocator::writeProtect(bool protect) const {
    for (size_t i = 0; i < _regionCount; ++i) {
        if (mprotect(_regionList[i].address, _regionList[i].size, protect ? PROT_READ : (PROT_READ | PROT_WRITE)) == -1) {
            //printf("FAILED: %d", errno);
        }
    }
}

bool Allocator::owned(const void* p, std::size_t nbytes) const
{
    const uintptr_t start = (uintptr_t)p;
    const uintptr_t end   = start+nbytes;
    for (size_t i=0; i < _regionCount; ++i) {
        uintptr_t regionStart = (uintptr_t)_regionList[i].address;
        uintptr_t regionEnd   = regionStart + _regionList[i].size;
        if ( (regionStart <= start) && (regionEnd >= end) )
            return true;
    }
    return false;
}


void* Allocator::malloc(size_t size) {
    return this->aligned_alloc(kGranuleSize, size);
}

AllocationMetadata* AllocationMetadata::getForPointer(void* data) {
    assert(data != nullptr);
    return (AllocationMetadata*)((uintptr_t)data-Allocator::kGranuleSize);
}

void* Allocator::aligned_alloc(size_t alignment, size_t size) {
    static_assert(sizeof(size_t) == sizeof(Allocator*), "Ensure size_t is pointer sized");
    static_assert(kGranuleSize >= (sizeof(size_t) == sizeof(Allocator*)), "Ensure we can fit all metadata in a granule");

    auto [storage, storageSize] = allocate_buffer(size, alignment, true);
    // We are guaranteed a 1 granule managed we can use for storage;
    auto metadata = new ((void*)((uintptr_t)storage-Allocator::kGranuleSize)) AllocationMetadata();
    metadata->allocator = this;
    metadata->size = storageSize/kGranuleSize;
    return storage;
}

void Allocator::free(void* ptr) {
    if (!ptr) { return; }
    // We are guaranteed a 1 granule managed we can use for storage;
    auto metadata = AllocationMetadata::getForPointer(ptr);
    assert(metadata->allocator == this);
    assert(metadata->slot1 == AllocationMetadata::kNormalPtr);
    this->deallocate_bytes((void*)metadata, (metadata->size+1)*kGranuleSize, kGranuleSize);
}

void staticFree(void* ptr) {
    if (!ptr) { return; }
    // We are guaranteed a 1 granule managed we can use for storage;
    auto metadata = AllocationMetadata::getForPointer(ptr);
    assert(metadata->slot1 == AllocationMetadata::kNormalPtr);
    metadata->allocator->deallocate_bytes((void*)metadata, (metadata->size+1)*Allocator::kGranuleSize, Allocator::kGranuleSize);
}

char* Allocator::strdup(const char* str)
{
    auto result = (char*)this->malloc(strlen(str)+1);
    strcpy(result, str);
    return result;
}

Allocator* Allocator::bootstrap() {
    Allocator allocator;
    Allocator* allocatorPtr = new (&allocator) Allocator();
    *allocatorPtr = std::move(allocator);
    return allocatorPtr;
}


} // namespace dyld4

void* operator new(std::size_t count, dyld4::Allocator* allocator) {
    return allocator->malloc(count);
}

void* operator new(std::size_t count, std::align_val_t al,dyld4::Allocator* allocator) {
    return allocator->aligned_alloc((size_t)al, count);
}

