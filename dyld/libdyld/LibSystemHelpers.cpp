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


#include <string.h>
#include <stdint.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <TargetConditionals.h>
#include <_simple.h>
#include <mach-o/dyld_priv.h>
#include <malloc/malloc.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_priv.h>
#include <dlfcn.h>
#include <dlfcn_private.h>
#include <libc_private.h>
#include <ptrauth.h>
#if !TARGET_OS_DRIVERKIT
  #include <vproc_priv.h>
#endif

// atexit header is missing C++ guards
extern "C" {
    #include <System/atexit.h>
}

// libc is missing header declaration for this
extern "C" int __cxa_atexit(void (*func)(void*), void* arg, void* dso);

#include "LibSystemHelpers.h"
#include "DyldProcessConfig.h"
#include "DyldAPIs.h"

// implemented in assembly
extern "C" void* tlv_get_addr(dyld3::MachOAnalyzer::TLV_Thunk*);


namespace dyld4 {

uintptr_t LibSystemHelpers::version() const
{
    return 2;
}

void* LibSystemHelpers::malloc(size_t size) const
{
    return ::malloc(size);
}

void LibSystemHelpers::free(void* p) const
{
    ::free(p);
}

size_t LibSystemHelpers::malloc_size(const void* p) const
{
    return ::malloc_size(p);
}

kern_return_t LibSystemHelpers::vm_allocate(vm_map_t task, vm_address_t* address, vm_size_t size, int flags) const
{
    return ::vm_allocate(task, address, size, flags);
}

kern_return_t LibSystemHelpers::vm_deallocate(vm_map_t task, vm_address_t address, vm_size_t size) const
{
    return ::vm_deallocate(task, address, size);
}

// Note: driverkit uses a different arm64e ABI, so we cannot call libSystem's pthread_key_create() from dyld
int LibSystemHelpers::pthread_key_create_free(pthread_key_t* key) const
{
    return ::pthread_key_create(key, &::free);
}

static void finalizeListTLV_thunk(void* list)
{
    // Called by pthreads when the current thread is going away
    gDyld.apis->_finalizeListTLV(list);
}

// Note: driverkit uses a different arm64e ABI, so we cannot call libSystem's pthread_key_create() from dyld
int LibSystemHelpers::pthread_key_create_thread_exit(pthread_key_t* key) const
{
    return ::pthread_key_create(key, &finalizeListTLV_thunk);
}

void* LibSystemHelpers::pthread_getspecific(pthread_key_t key) const
{
    return ::pthread_getspecific(key);
}

int LibSystemHelpers::pthread_setspecific(pthread_key_t key, const void* value) const
{
    return ::pthread_setspecific(key, value);
}

void LibSystemHelpers::__cxa_atexit(void (*func)(void*), void* arg, void* dso) const
{
#if !__arm64e__
    // Note: for arm64e driverKit uses a different ABI for function pointers,
    // but dyld does not support static terminators for arm64e
    ::__cxa_atexit(func, arg, dso);
#endif
}

void LibSystemHelpers::__cxa_finalize_ranges(const __cxa_range_t ranges[], unsigned int count) const
{
    ::__cxa_finalize_ranges(ranges, count);
}

bool LibSystemHelpers::isLaunchdOwned() const
{
#if TARGET_OS_DRIVERKIT
    return false;
#else
    // the vproc_swap_integer() call has to be to libSystem.dylib's function - not a static copy in dyld
    int64_t val = 0;
    ::vproc_swap_integer(nullptr, VPROC_GSK_IS_MANAGED, nullptr, &val);
	return ( val != 0 );
#endif
}

void LibSystemHelpers::os_unfair_recursive_lock_lock_with_options(os_unfair_recursive_lock_t lock, os_unfair_lock_options_t options) const
{
    ::os_unfair_recursive_lock_lock_with_options(lock, options);
}

void LibSystemHelpers::os_unfair_recursive_lock_unlock(os_unfair_recursive_lock_t lock) const
{
    ::os_unfair_recursive_lock_unlock(lock);
}

void LibSystemHelpers::exit(int result) const
{
    ::exit(result);
}

const char* LibSystemHelpers::getenv(const char* key) const
{
    return ::getenv(key);
}

int LibSystemHelpers::mkstemp(char* templatePath) const
{
    return ::mkstemp(templatePath);
}

LibSystemHelpers::TLVGetAddrFunc LibSystemHelpers::getTLVGetAddrFunc() const
{
    return &::tlv_get_addr;
}

// Added in version 2

void LibSystemHelpers::os_unfair_recursive_lock_unlock_forked_child(os_unfair_recursive_lock_t lock) const
{
    ::os_unfair_recursive_lock_unlock_forked_child(lock);
}

} // namespace dyld4

