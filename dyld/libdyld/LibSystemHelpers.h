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

#ifndef _libSystem_h
#define _libSystem_h

#include <pthread.h>
#include <unistd.h>
#include <malloc/malloc.h>
#include <os/lock_private.h>
#include <mach/mach.h>

#include "MachOAnalyzer.h"
#include "Defines.h"


namespace dyld4 {

//
// Helper for performing "up calls" from dyld into libSystem.dylib.
//
// Note: driverkit and base OS use the same dyld, but different libdyld.dylibs.  We use the clang attribute
// to ensure both libdyld implementations use the same vtable pointer authentication. Similarly, we cannot use
// the generic pthread_key_create() because it takes a clean function pointer parameter and the authentication
// for that may differ in the two libdyld.dylibs.
//
struct VIS_HIDDEN  [[clang::ptrauth_vtable_pointer(process_independent, address_discrimination, type_discrimination)]] LibSystemHelpers
{
    typedef void  (*ThreadExitFunc)(void* storage);
    typedef void* (*TLVGetAddrFunc)(dyld3::MachOAnalyzer::TLV_Thunk*);

    virtual uintptr_t       version() const;
    virtual void*           malloc(size_t size) const;
    virtual void            free(void* p) const;
    virtual size_t          malloc_size(const void* p) const;
    virtual kern_return_t   vm_allocate(vm_map_t target_task, vm_address_t* address, vm_size_t size, int flags) const;
    virtual kern_return_t   vm_deallocate(vm_map_t target_task, vm_address_t address, vm_size_t size) const;
    virtual int             pthread_key_create_free(pthread_key_t* key) const;
    virtual int             pthread_key_create_thread_exit(pthread_key_t* key) const;
    virtual void*           pthread_getspecific(pthread_key_t key) const;
    virtual int             pthread_setspecific(pthread_key_t key, const void* value) const;
    virtual void            __cxa_atexit(void (*func)(void*), void* arg, void* dso) const;
    virtual void            __cxa_finalize_ranges(const struct __cxa_range_t ranges[], unsigned int count) const;
    virtual bool            isLaunchdOwned() const;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
    virtual void            os_unfair_recursive_lock_lock_with_options(os_unfair_recursive_lock_t lock, os_unfair_lock_options_t options) const;
    virtual void            os_unfair_recursive_lock_unlock(os_unfair_recursive_lock_t lock) const;
#pragma clang diagnostic pop
    virtual void            exit(int result) const  __attribute__((noreturn));
    virtual const char*     getenv(const char* key) const;
    virtual int             mkstemp(char* templatePath) const;
    virtual TLVGetAddrFunc  getTLVGetAddrFunc() const;

    // Added in version 2
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
    virtual void            os_unfair_recursive_lock_unlock_forked_child(os_unfair_recursive_lock_t lock) const;
#pragma clang diagnostic pop
};

} // namespace

// implemented in DyldAPIs.cpp to record helpers into RuntimeState object
extern "C" void _libdyld_initialize(const dyld4::LibSystemHelpers*);

#endif /* _libSystem_h */

