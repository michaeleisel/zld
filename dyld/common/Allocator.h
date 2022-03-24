//
//  Allocactor.h
//  DRL
//
//  Created by Louis Gerbarg on 3/28/20.
//  Copyright © 2020 Louis Gerbarg. All rights reserved.
//

#ifndef DRL_Allocator_h
#define DRL_Allocator_h

#if BUILDING_LIBDYLD
#define ENABLE_ALLOCATOR_LOCKING (1)
#else
#define ENABLE_ALLOCATOR_LOCKING (0)
#endif

#include <limits>
#include <cassert>
#include <cstddef>

#if ENABLE_ALLOCATOR_LOCKING
#include <atomic>
#include <os/lock.h>
#endif

#include "Defines.h"

//TODO: Implement UniquePtr <-> SharedPtr adoption
//TODO: Implement UniquePtr[] to cut down allocator load
//TODO: Get prefixed pointers working for large allocations
//TODO: WeakPtr support (since the allocator supports partial returns we can support very efficient zeroing weak refs)
//TODO: MallocStackLogging support
//TODO: Consider moving to concurent bitmaps (ld64 support)
//TODO: Add Large allocation support for managed pointers

// This defines the underlying interface for DRL's polymorphic allocators, along with several concrete allocators
// Unlike STL's allocators these only support allocating untype's bytes. Object management is handled by the containers
// that use them.

namespace dyld4 {

struct Allocator;

// This should be part of Allocator, but it cannot be because you cannot forward declare methods in a class, and UniquePtr needs it
// UniquePtr has to come before Allocator because it is a tempalte class and template classes cannot be forward declared
VIS_HIDDEN
void staticFree(void*);

// This is an internal metadata struct the allocator uses to support malloc/free style interfaces, as well as smart pointers
struct VIS_HIDDEN AllocationMetadata {
    Allocator *allocator    = nullptr;
    uint16_t  size          = 0;
    uint16_t  slot1         = 0;    // For now use this as a pointer type tag
    uint16_t  slot2         = 0;    // SharedPtr refCount
    uint16_t  slot3         = 0;    // SharedPtr refCount
    static AllocationMetadata* getForPointer(void*);
    static const uint16_t kNormalPtr = 0;
    static const uint16_t kSharedPtr = 1;
    static const uint16_t kUniquePtr = 2;
};

template<typename T>
struct VIS_HIDDEN UniquePtr {
    UniquePtr() = default;
    constexpr UniquePtr(std::nullptr_t) : UniquePtr() {};
    UniquePtr(T* D) : _data(D) {
        if (!_data) { return; }
        auto metadata = AllocationMetadata::getForPointer((void*)_data);
        assert(metadata->slot1 == AllocationMetadata::kNormalPtr);
        metadata->slot1 = AllocationMetadata::kUniquePtr;
    }
    UniquePtr(const UniquePtr&) = delete;
    void operator=(const UniquePtr&) = delete;

    UniquePtr(UniquePtr&& O) {
        std::swap(_data, O._data);
    }
    void operator=(UniquePtr&& O) {
        std::swap(_data, O._data);
    };
    ~UniquePtr() {
        if (!_data) { return; }
        auto metadata = AllocationMetadata::getForPointer((void*)_data);
        assert(metadata->slot1 == AllocationMetadata::kUniquePtr);
        get()->~T();
        metadata->slot1 = AllocationMetadata::kNormalPtr;
        dyld4::staticFree((void*)_data);
    }
    explicit operator bool() {
        return (_data != nullptr);
    }
    T& operator*() {
        return *((T*)_data);
    }
    T* operator->() {
        return (T*)_data;
    }
    const T& operator*() const {
        return *((const T*)_data);
    }
    const T* operator->() const {
        return (const T*)_data;
    }
    T* get() {
        return (T*)_data;
    }
    T* release() {
        if (!_data) { return nullptr; }
        auto metadata = AllocationMetadata::getForPointer(_data);
        metadata->slot1 = AllocationMetadata::kNormalPtr;
        T* result = (T*)_data;
        _data = nullptr;
        return result;
    }

private:
    T*   _data = nullptr;
};

template<typename T>
struct VIS_HIDDEN SharedPtr {
    SharedPtr() = default;
    constexpr SharedPtr(std::nullptr_t) : SharedPtr() {};
    SharedPtr(T* D) : _data(D) {
        if (!_data) { return; }
        auto metadata = AllocationMetadata::getForPointer(_data);
        // We support implicit shared_from_this, so we might be passed in a point that is either normal or shared
        assert(metadata->slot1 == AllocationMetadata::kNormalPtr || metadata->slot1 == AllocationMetadata::kSharedPtr);
        metadata->slot1 = AllocationMetadata::kSharedPtr;
        incrementRefCount();
    }
    SharedPtr(const SharedPtr& O) : _data(O._data) {
        incrementRefCount();
    };
    void operator=(const SharedPtr& O) {
        _data = O._data;
        incrementRefCount();
    }
    SharedPtr(SharedPtr&& O) {
        std::swap(_data, O._data);
    }
    void operator=(SharedPtr&& O) {
        std::swap(_data, O._data);
    };
    ~SharedPtr() {
        decrementRefCount();
    }
    explicit operator bool() {
        return (_data != nullptr);
    }
    T& operator*() {
        return *((T*)_data);
    }
    T* operator->() {
        return (T*)_data;
    }
    const T& operator*() const {
        return *((const T*)_data);
    }
    const T* operator->() const {
        return (const T*)_data;
    }
    T* get() {
        return (T*)_data;
    }
private:
    void incrementRefCount() {
        if (!_data) { return; }
        auto metadata = AllocationMetadata::getForPointer(_data);
        assert(metadata->slot1 == AllocationMetadata::kSharedPtr);
#if ENABLE_ALLOCATOR_LOCKING
        auto refCount = (std::atomic<uint32_t>*)&metadata->slot2;
        refCount->fetch_add(1, std::memory_order_relaxed);
#else
        auto refCount = (uint32_t*)&metadata->slot2;
        ++*refCount;
#endif
    }
    void decrementRefCount() {
        if (!_data) { return; }
        auto metadata = AllocationMetadata::getForPointer(_data);
        assert(metadata->slot1 == AllocationMetadata::kSharedPtr);
#if ENABLE_ALLOCATOR_LOCKING
        auto refCount = (std::atomic<uint32_t>*)&metadata->slot2;
        if (refCount->fetch_sub(1, std::memory_order_release) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            get()->~T();
            metadata->slot1 = AllocationMetadata::kNormalPtr;
            dyld4::staticFree((void*)_data);
        }
#else
        uint32_t *refCount = (uint32_t*)&metadata->slot2;
        --*refCount;
        if (*refCount == 0) {
            get()->~T();
            metadata->slot1 = AllocationMetadata::kNormalPtr;
            dyld4::staticFree((void*)_data);
        }
#endif
    }
    T*   _data = nullptr;

    //TODO: Add layour asserts to ensure slot2 is aligned for atomics;
};

// TODO: I have a design for a higher performance best fit alloactor based on sparse bitmaps
// The complexity does not justify the effort for dyld, but it will be worth it if we bring
// DRL collections and allocators to ld64

// This is a conventional best fit free list allocator. It is capable of returning unused regions
// back to its upstream allocator, and if the upstream alloactor returns to adjacent regions it,
// can return buffers that straddle them.
//
// We maintain a list of upstream allocated regions to ensure we always deallocate on the same boundaries
// in order to prevent guard exceptions.
struct VIS_HIDDEN Allocator
{
    /* We hardcode the page size instead of using PAGE_SIZE because pagesize resolve to external symbol, which prevents us
     * from usng it in static_asserts
     */
    #if __arm64__
    static const std::size_t    kPageSize           = (16*1024);
    #else
    static const std::size_t    kPageSize           = (4*1024);
    #endif
    static const std::size_t    kPoolSize           = (1024*1024);
    static const std::size_t    kGranuleSize        = (16);

    // a tuple of an allocated <pointer, size>
    struct Buffer {
        void*   address = nullptr;
        size_t  size    = 0;

        bool    operator==(const Buffer&) const;
        bool    operator<(const Buffer&) const;
        void*   lastAddress() const;                // end() ??
        bool    contains(const Buffer&) const;
        bool    valid() const;
        void    dump() const;
    };

                            Allocator() = default;
                            ~Allocator();
    // Simple interfaces
    //   These present an interface similiar to normal malloc, and return managed pointers than can be used
    //   with the SharedPtr and UniquePtr classes
    void*                   malloc(size_t size);
    void*                   aligned_alloc(size_t alignment, size_t size);
    void                    free(void* ptr);
    char*                   strdup(const char*);

    // Advanced Interfaces
    //   These interface return raw regions of memory from the allocator. They are not compatible with SharedPtr and UniquePtr.

    // allocate_bytes() takes a size and required alignment. The alignment must be a power of 2
    // it returns a tuple of pointer to the allocated memory, and the actual size of the memory allocated
    // this allows allocators that return large granules (such as the vm_allocate backed allocator) to inform
    // the caller of the actual size returned, it case it can use the additional space.
    [[nodiscard]] Buffer    allocate_buffer(std::size_t nbytes, std::size_t alignment = 16);
    
    // Deallocates a buffer returned from allocate_bytes
    void                    deallocate_buffer(Buffer buffer);

    // This is an advanced interface intended for allocators that do not store the returned size, but instead
    // dynamic generate the correct value through making sure to pass equivalent alignments into this and allocate_buffer
    void                    deallocate_bytes(void* p, std::size_t nbytes, std::size_t alignment);
    std::size_t             allocated_bytes();
    void                    writeProtect(bool protect) const;
    bool                    owned(const void* p, std::size_t nbytes) const; // checks if p is owned by the Allocator

    // smart pointers
    template< class T, class... Args >
    UniquePtr<T> makeUnique(Args&&... args ) {
        return UniquePtr<T>(new (this) T(std::forward<Args>(args)...));
    }

    template< class T, class... Args >
    SharedPtr<T> makeShared(Args&&... args ) {
        return SharedPtr<T>(new (this) T(std::forward<Args>(args)...));
    }
    
    // static methods
    static Allocator*       bootstrap(); // Initializes an pool and host the Allocator within that poo;

    // Debug methods
    void                    validateFreeList();
    void                    dumpFreeList();
    void                    dumpRegionList();
private:
    template<typename T> friend struct UniquePtr;

    void                    returnSpace(Buffer region, bool deallocate, bool guard);
    Buffer                  reserveSpace(std::size_t nbytes, std::size_t alignment, bool prefix, bool guard);
    // prefix will ensure that a 1 granule area before the returned buffer is also valid and is reserved. This space can be used
    // for metadat necessry to implemented shared pointers, implement a malloc()/free() style interface, etc.
    [[nodiscard]] Buffer    allocate_buffer(std::size_t nbytes, std::size_t alignment, bool prefix);

    void                    removeRegion(const Buffer& removedRegion);
#if ENABLE_ALLOCATOR_LOCKING
    template<typename F>
    void withLockedFreeList(F f) {
        os_unfair_lock_lock(&_lock);
        f();
        os_unfair_lock_unlock(&_lock);
    }
#else
    template<typename F>
    void withLockedFreeList(F f) {
        f();
    }
#endif
    Allocator&              operator=(Allocator&&);

    // A free list entry is essentially a region. The one special property is that they form a linked list,
    // so, if you dereference the address field you will get the next free list entry. This means that both
    // the address AND the size of the a FreeListEntry are stored in the entry before it. This is a bit
    // non-intuitive compared to storing the length of the current entry and a pointer to the next, but it
    // it simplifies a lot of code.
    struct FreeListEntry : Buffer {
                FreeListEntry() = default;
                FreeListEntry(const FreeListEntry&) = default;
                FreeListEntry(const Buffer&);

        FreeListEntry*  split(size_t entrySize);
        bool            mergeNext();
        void            isolateRegion(Buffer region);
        FreeListEntry*  next() const;
    };
    [[nodiscard]] Buffer    vm_allocate_bytes(std::size_t sizet);
    void                    vm_deallocate_bytes(void* p, std::size_t size);
    size_t                  roundToGranule(size_t size);
    size_t                  roundToPage(size_t size);

    FreeListEntry           _freeListHead;
    std::size_t             _allocatedBytes = 0;
    std::size_t             _vmAllocatedBytes = 0;
    Buffer*                 _regionList     = nullptr;
    std::size_t             _regionCount    = 0;
#if ENABLE_ALLOCATOR_LOCKING
    os_unfair_lock          _lock           = OS_UNFAIR_LOCK_INIT;
#endif
    static_assert(kPageSize%kPageSize == 0, "Page size must be a multiple of the granule size");
    static_assert(kPoolSize%kPageSize == 0, "Pool size must be a multiple of the page size");
    static_assert(sizeof(FreeListEntry) <= kGranuleSize, "Granule must be large enough to hold a free list entry");
    
    static_assert(sizeof(AllocationMetadata) <= kPoolSize, "Granule must be large enough to hold AllocationMetadata");
    static_assert(std::numeric_limits<uint64_t>::max()*kGranuleSize >= kPoolSize, "Size field must be large enough to largest pool allocated object");
    static_assert(alignof(AllocationMetadata) <= kGranuleSize, "AllocationMetadata must be naturally aligned ona granule");
};

} // namespace dyld4

// We do not include operator delete because it is incredibly painful to use, and is usually not the correct answer. Any points returned via
// operator new (sAllocator) should be freed via sAllocator->free() or staticFree()

VIS_HIDDEN void* operator new(std::size_t count, dyld4::Allocator* allocator);
VIS_HIDDEN void* operator new(std::size_t count, std::align_val_t al,dyld4::Allocator* allocator);

#endif /* DRL_Allocator_h */
