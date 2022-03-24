/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2016 Apple Inc. All rights reserved.
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

#ifndef ProcessAtlas_h
#define ProcessAtlas_h

#include <atomic>
#include <cstdint>
#include <mach/mach.h>
#include <dispatch/dispatch.h>

#include "MachOLoaded.h"
#include "UUID.h"
#include "Allocator.h"
#include "Vector.h"

#define VIS_HIDDEN __attribute__((visibility("hidden")))

class DyldSharedCache;

using namespace dyld3;

namespace dyld4 {
namespace Atlas {

/* The Mapper abstraction provides an interface we can use to abstract away in memory vs file layout for the cache
 *
 * All of the code is written as though the mach-o and cache files are mapped and loaded. When possible we reuse
 * dylibs from within the current process using a LocalMapper. When that is not possible we will go to disk using
 * a FileMapper. We never map remote memory.
 */

struct VIS_HIDDEN Mapper {
    // Move only smart pointer to manage mapped memory allocations
    template<typename T>
    struct Pointer {
        Pointer() = default;
        Pointer(const Pointer&) = delete;
        Pointer& operator=(const Pointer&) = delete;

        Pointer(SharedPtr<Mapper>& M, const void* A, uint64_t S) : _mapper(M), _size(S) {
            auto [pointer, mmaped] = _mapper->map(A,_size);
            _pointer = pointer;
            _mmapped = mmaped;
        }
        Pointer(Pointer&& P) : _mapper(P._mapper) {
            std::swap(_size, P._size);
            std::swap(_pointer, P._pointer);
            std::swap(_mmapped, P._mmapped);
        }
        void operator=(Pointer&& P) {
            std::swap(_mapper, P._mapper);
            std::swap(_size, P._size);
            std::swap(_pointer, P._pointer);
            std::swap(_mmapped, P._mmapped);
        };
        ~Pointer() {
            if (_pointer && _mmapped) {
                _mapper->unmap(_pointer, _size);
            }
        }
        explicit operator bool() {
            return (_pointer != nullptr);
        }
        T& operator*() {
            return *((T*)_pointer);
        }
        T* operator->() {
            return (T*)_pointer;
        }

        const T& operator*() const {
            return *((const T*)_pointer);
        }
        const T* operator->() const {
            return (const T*)_pointer;
        }
    private:
        SharedPtr<Mapper>   _mapper     = nullptr;
        uint64_t            _size       = 0;
        void*               _pointer    = nullptr;
        bool                _mmapped    = false;
    };

    ~Mapper();
    template<typename T>
    Pointer<T>  map(const void* addr, uint64_t size) {
        auto mapper = SharedPtr<Mapper>(this);
        return Pointer<T>(mapper, addr, size);
    }
    struct Mapping {
        uint64_t    offset;
        uint64_t    size;
        uint64_t    address;
        int         fd; // If fd == -1 that means this is a memory mapping
    };

    static SharedPtr<Mapper>                        mapperForLocalSharedCache(const char* cachePath, const DRL::UUID& uuid, const void* baseAddress);
    static SharedPtr<Mapper>                        mapperForSharedCache(const char* filePath, const DRL::UUID& uuid, const void* baseAddress);
    static std::pair<SharedPtr<Mapper>,uint64_t>    mapperForSharedCacheLocals(const char* filePath);
    Mapper();
    Mapper(uint64_t S);
    Mapper(const Vector<Mapping>&  M);
    const void*                                     baseAddress() const;
    const uint64_t                                  size() const;
    bool                                            pin();
    void                                            unpin();
private:
    std::pair<void*,bool>                           map(const void* addr, uint64_t size) const;
    void                                            unmap(const void* addr, uint64_t size) const;
    Vector<Mapping>                                 _mappings;
    void*                                           _flatMapping;
};


struct SharedCache;

struct VIS_HIDDEN Image {
                        Image() = default;
//                        Image& operator=(const Image&);

                        Image(SharedPtr<Mapper>& M, void* CA, uint64_t S, const SharedCache* SC);
    const DRL::UUID&    uuid();
    const char*         installname();
    const char*         filename();
    const SharedCache*  sharedCache()const;
    uint64_t            sharedCacheVMOffset() const;
    uint32_t            pointerSize();
    bool                forEachSegment(void (^block)(const char* segmentName, uint64_t vmAddr, uint64_t vmSize,  int perm));
    bool                forEachSection(void (^block)(const char* segmentName, const char* sectionName, uint64_t vmAddr, uint64_t
                                                     vmSize));
    bool                contentForSegment(const char* segmentName,
                                          void (^contentReader)(const void* content, uint64_t vmAddr, uint64_t vmSize));
    bool                contentForSection(const char* segmentName, const char* sectionName,
                                          void (^contentReader)(const void* content, uint64_t vmAddr, uint64_t vmSize));
private:
    const MachOLoaded*              ml();
    DRL::UUID                       _uuid;
    Mapper::Pointer<MachOLoaded>    _ml;
    const uint64_t                  _slide              = 0;
    const void*                     _address            = nullptr;
    SharedPtr<Mapper>               _mapper;
    const SharedCache*              _sharedCache        = nullptr;
    const char*                     _installname        = nullptr;
    const char*                     _filename           = nullptr;
    bool                            _uuidLoaded         = false;
    bool                            _installnameLoaded  = false;
    bool                            _filenameLoaded     = false;
};

struct VIS_HIDDEN SharedCacheLocals {
                                            SharedCacheLocals(SharedPtr<Mapper>& M, bool use64BitDylibOffsets);
    const dyld_cache_local_symbols_info*    localInfo() const;
    bool                                     use64BitDylibOffsets() const;
private:
    friend struct SharedCache;


    SharedPtr<Mapper>                   _mapper;
    Mapper::Pointer<uint8_t>            _locals;
    bool                                _use64BitDylibOffsets = false;
};

struct VIS_HIDDEN SharedCache {
                                    SharedCache(SharedPtr<Mapper>& M, const char* FP, bool P);
    const DRL::UUID&                uuid() const;
    uint64_t                        baseAddress() const;
    uint64_t                        size() const;
    void                            forEachFilePath(void (^block)(const char* file_path)) const;
    bool                            isPrivateMapped() const;
    static UniquePtr<SharedCache>   createForFilePath(const char* filePath);
    void                            forEachImage(void (^block)(Image* image));
    UniquePtr<SharedCacheLocals>    localSymbols() const;
    bool                            pin();
    void                            unpin();

#ifdef TARGET_OS_OSX
    bool                            mapSubCacheAndInvokeBlock(const dyld_cache_header* subCacheHeader, void (^block)(const void* cacheBuffer, size_t size));
    bool                            forEachSubcache4Rosetta(void (^block)(const void* cacheBuffer, size_t size));
#endif

    static void                     forEachInstalledCacheWithSystemPath(const char* systemPath, void (^block)(SharedCache* cache));
private:
    friend struct ProcessSnapshot;

    static UniquePtr<SharedCache>   createForTask(task_read_t task, kern_return_t *kr);

    DRL::UUID                           _uuid;
    uint64_t                            _size;
    Vector<UniquePtr<const char>>       _files;
    bool                                _private;
    Vector<UniquePtr<Image>>            _images;
    Mapper::Pointer<dyld_cache_header>  _header;
    SharedPtr<Mapper>                   _mapper;
    uint64_t                            _slide      = 0;
};

#if BUILDING_LIBDYLD_INTROSPECTION || BUILDING_LIBDYLD || BUILDING_CACHE_BUILDER || BUILDING_UNIT_TESTS
struct VIS_HIDDEN ProcessSnapshot {
                                        ProcessSnapshot(task_read_t task, kern_return_t *kr);
    static UniquePtr<ProcessSnapshot>   createForTask(task_read_t task, kern_return_t *kr);
    UniquePtr<SharedCache>&             sharedCache();
    void                                forEachImage(void (^block)(Image* image));
private:
    task_read_t                 _task;
    UniquePtr<SharedCache>      _sharedCache;
    Vector<Image>               _images;
};

struct VIS_HIDDEN Process {
                                Process(task_read_t task, kern_return_t *kr);
                                ~Process();
    static UniquePtr<Process>   createForCurrentTask();
    static UniquePtr<Process>   createForTask(task_read_t task, kern_return_t *kr);

    uint32_t                    registerEventHandler(kern_return_t *kr, uint32_t event, dispatch_queue_t queue, void (^block)());
    void                        unregisterEventHandler(uint32_t handle);
    UniquePtr<ProcessSnapshot>  createSnapshot(kern_return_t *kr);
private:
    struct ProcessNotifierRecord {
        dispatch_queue_t    queue;
        void                (^block)();
        uint32_t            notifierID;
    };
    enum ProcessNotifierState {
        Disconnected = 0,
        Connected,
        Disconnecting
    };
    void                        setupNotifications(kern_return_t *kr);
    void                        teardownNotifications();
    void                        handleNotifications();
    task_read_t                                     _task       = TASK_NULL;
    mach_port_t                                     _port       = MACH_PORT_NULL;
    dispatch_queue_t                                _queue      = NULL;
    dispatch_source_t                               _machSource = NULL;
    ProcessNotifierState                            _state      = Disconnected;
    //FIXME: This should be a map to make it easier to cleanup
    Vector<ProcessNotifierRecord>                   _registeredNotifiers;
};
#endif /* BUILDING_LIBDYLD_INTROSPECTION || BUILDING_LIBDYLD */

};
};

#endif /* ProcessAtlas_h */
