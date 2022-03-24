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
#include <pthread.h>

#include "DyldProcessConfig.h"
#include "DyldAPIs.h"

using dyld4::gDyld;


// implemented in assembly
extern "C" void* tlv_get_addr(dyld3::MachOAnalyzer::TLV_Thunk*);

// called from threadLocalHelpers.s
extern "C" void* instantiateTLVs_thunk(pthread_key_t key);
VIS_HIDDEN
void* instantiateTLVs_thunk(pthread_key_t key)
{
    // Called by _tlv_get_addr on slow path to allocate thread
    // local storge for the current thread.
    return gDyld.apis->_instantiateTLVs(key);
}

#if SUPPPORT_PRE_LC_MAIN
// called by crt before main() by programs linked with 10.4 or earlier crt1.o
static void _dyld_make_delayed_module_initializer_calls()
{
    // We don't actually do anything here, we just need the function to exist
    // If we had a very old binary AND a custom entry point we would have to do something, but dyld has not supported that on x86_64 in years.
    // Instead just return an empty function and let initializers run normally
    gDyld.apis->runAllInitializersForMain();
}
#endif

// Used to support legacy binaries that have __DATA,__dyld sections
static int legacyDyldLookup4OldBinaries(const char* name, void** address)
{
#if SUPPPORT_PRE_LC_MAIN
    if (strcmp(name, "__dyld_dlopen") == 0) {
        *address = (void*)&dlopen;
        return true;
    } else if (strcmp(name, "__dyld_dlsym") == 0) {
        *address = (void*)&dlsym;
        return true;
    } else if (strcmp(name, "__dyld_dladdr") == 0) {
        *address = (void*)&dladdr;
        return true;
    } else if (strcmp(name, "__dyld_get_image_slide") == 0) {
        *address = (void*)&_dyld_get_image_slide;
        return true;
    } else if (strcmp(name, "__dyld_make_delayed_module_initializer_calls") == 0) {
        *address = (void*)&_dyld_make_delayed_module_initializer_calls;
        return true;
    } else if (strcmp(name, "__dyld_lookup_and_bind") == 0) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        *address = (void*)&_dyld_lookup_and_bind;
#pragma clang diagnostic pop
        return true;
    }
#endif
    *address = 0;
    return false;
}

// this is the magic __DATA,__dyld4 section that dyld and libdyld.dylib use to rendezvous
namespace dyld4 {
    volatile LibdyldDyld4Section gDyld __attribute__((used, visibility("hidden"), section ("__DATA,__dyld4")))
        = { nullptr, nullptr, { nullptr, &NXArgc, &NXArgv, (const char***)&environ, &__progname}, &legacyDyldLookup4OldBinaries
        };
}

using dyld4::gDyld;


static const dyld4::LibSystemHelpers sHelpers;

static void beforeForkPrepareDlopen()
{
    gDyld.apis->_dyld_before_fork_dlopen();
}

static void afterForkParentDlopen()
{
    gDyld.apis->_dyld_after_fork_dlopen_parent();
}

static void afterForkChildDlopen()
{
    gDyld.apis->_dyld_after_fork_dlopen_child();
}

// This is called during libSystem.dylib initialization.
// It calls back into dyld and lets it know it can start using libSystem.dylib
// functions which are wrapped in the LibSystemHelpers class.
void _dyld_initializer()
{
    gDyld.apis->_libdyld_initialize(&sHelpers);

    pthread_atfork(&beforeForkPrepareDlopen, &afterForkParentDlopen, &afterForkChildDlopen);
}



//
// MARK: --- APIs from macOS 10.2 ---
//
uint32_t _dyld_image_count()
{
    return gDyld.apis->_dyld_image_count();
}

const mach_header* _dyld_get_image_header(uint32_t index)
{
    return gDyld.apis->_dyld_get_image_header(index);
}

intptr_t _dyld_get_image_vmaddr_slide(uint32_t index)
{
    return gDyld.apis->_dyld_get_image_vmaddr_slide(index);
}

const char* _dyld_get_image_name(uint32_t index)
{
    return gDyld.apis->_dyld_get_image_name(index);
}

void _dyld_register_func_for_add_image(void (*func)(const mach_header* mh, intptr_t vmaddr_slide))
{
    gDyld.apis->_dyld_register_func_for_add_image(func);
}

void _dyld_register_func_for_remove_image(void (*func)(const mach_header* mh, intptr_t vmaddr_slide))
{
    gDyld.apis->_dyld_register_func_for_remove_image(func);
}

int32_t NSVersionOfLinkTimeLibrary(const char* libraryName)
{
    return gDyld.apis->NSVersionOfLinkTimeLibrary(libraryName);
}

int32_t NSVersionOfRunTimeLibrary(const char* libraryName)
{
    return gDyld.apis->NSVersionOfRunTimeLibrary(libraryName);
}

int _NSGetExecutablePath(char* buf, uint32_t* bufsize)
{
    return gDyld.apis->_NSGetExecutablePath(buf, bufsize);
}

void _dyld_fork_child()
{
    gDyld.apis->_dyld_fork_child();
}

//
// MARK: --- APIs from macOS 10.4 ---
//
int dladdr(const void* addr, Dl_info* result)
{
    return gDyld.apis->dladdr(addr, result);
}

#if !TARGET_OS_DRIVERKIT
void* dlopen(const char* path, int mode)
{
    return gDyld.apis->dlopen(path, mode);
}

int dlclose(void* handle)
{
    return gDyld.apis->dlclose(handle);
}

char* dlerror()
{
    return gDyld.apis->dlerror();
}

void* dlsym(void* handle, const char* symbol)
{
    return gDyld.apis->dlsym(handle, symbol);
}

bool dlopen_preflight(const char* path)
{
    return gDyld.apis->dlopen_preflight(path);
}
#endif

//
// MARK: --- APIs deprecated in macOS 10.5 and not on any other platform ---
//
#if TARGET_OS_OSX
NSObjectFileImageReturnCode NSCreateObjectFileImageFromFile(const char* pathName, NSObjectFileImage* objectFileImage)
{
    return gDyld.apis->NSCreateObjectFileImageFromFile(pathName, objectFileImage);
}

NSObjectFileImageReturnCode NSCreateObjectFileImageFromMemory(const void* address, size_t size, NSObjectFileImage* objectFileImage)
{
    return gDyld.apis->NSCreateObjectFileImageFromMemory(address, size, objectFileImage);
}

bool NSDestroyObjectFileImage(NSObjectFileImage objectFileImage)
{
    return gDyld.apis->NSDestroyObjectFileImage(objectFileImage);
}

uint32_t NSSymbolDefinitionCountInObjectFileImage(NSObjectFileImage objectFileImage)
{
    gDyld.apis->obsolete();
    return 0;
}

const char* NSSymbolDefinitionNameInObjectFileImage(NSObjectFileImage objectFileImage, uint32_t ordinal)
{
    gDyld.apis->obsolete();
    return nullptr;
}

uint32_t NSSymbolReferenceCountInObjectFileImage(NSObjectFileImage objectFileImage)
{
    gDyld.apis->obsolete();
    return 0;
}

const char* NSSymbolReferenceNameInObjectFileImage(NSObjectFileImage objectFileImage, uint32_t ordinal, bool* tentative_definition)
{
    gDyld.apis->obsolete();
    return nullptr;
}

bool NSIsSymbolDefinedInObjectFileImage(NSObjectFileImage objectFileImage, const char* symbolName)
{
    return gDyld.apis->NSIsSymbolDefinedInObjectFileImage(objectFileImage, symbolName);
}

void* NSGetSectionDataInObjectFileImage(NSObjectFileImage objectFileImage, const char* segmentName, const char* sectionName, size_t* size)
{
    return gDyld.apis->NSGetSectionDataInObjectFileImage(objectFileImage, segmentName, sectionName, size);
}

const char* NSNameOfModule(NSModule m)
{
    return gDyld.apis->NSNameOfModule(m);
}

const char* NSLibraryNameForModule(NSModule m)
{
    return gDyld.apis->NSLibraryNameForModule(m);
}

NSModule NSLinkModule(NSObjectFileImage objectFileImage, const char* moduleName, uint32_t options)
{
    return gDyld.apis->NSLinkModule(objectFileImage, moduleName, options);
}

bool NSUnLinkModule(NSModule module, uint32_t options)
{
    return gDyld.apis->NSUnLinkModule(module, options);
}

bool NSIsSymbolNameDefined(const char* symbolName)
{
    return gDyld.apis->NSIsSymbolNameDefined(symbolName);
}

bool NSIsSymbolNameDefinedWithHint(const char* symbolName, const char* libraryNameHint)
{
    return gDyld.apis->NSIsSymbolNameDefinedWithHint(symbolName, libraryNameHint);
}

bool NSIsSymbolNameDefinedInImage(const mach_header* image, const char* symbolName)
{
    return gDyld.apis->NSIsSymbolNameDefinedInImage(image, symbolName);
}

NSSymbol NSLookupAndBindSymbol(const char* symbolName)
{
    return gDyld.apis->NSLookupAndBindSymbol(symbolName);
}

NSSymbol NSLookupAndBindSymbolWithHint(const char* symbolName, const char* libraryNameHint)
{
    return gDyld.apis->NSLookupAndBindSymbolWithHint(symbolName, libraryNameHint);
}

NSSymbol NSLookupSymbolInModule(NSModule module, const char* symbolName)
{
    return gDyld.apis->NSLookupSymbolInModule(module, symbolName);
}

NSSymbol NSLookupSymbolInImage(const mach_header* image, const char* symbolName, uint32_t options)
{
    return gDyld.apis->NSLookupSymbolInImage(image, symbolName, options);
}

const char* NSNameOfSymbol(NSSymbol symbol)
{
    gDyld.apis->obsolete();
    return nullptr;
}

void* NSAddressOfSymbol(NSSymbol symbol)
{
    return gDyld.apis->NSAddressOfSymbol(symbol);
}

NSModule NSModuleForSymbol(NSSymbol symbol)
{
    return gDyld.apis->NSModuleForSymbol(symbol);
}

void NSLinkEditError(NSLinkEditErrors* c, int* errorNumber, const char** fileName, const char** errorString)
{
    gDyld.apis->NSLinkEditError(c, errorNumber, fileName, errorString);
}

void NSInstallLinkEditErrorHandlers(const NSLinkEditErrorHandlers* handlers)
{
    gDyld.apis->obsolete();
}

bool NSAddLibrary(const char* pathName)
{
    return gDyld.apis->NSAddLibrary(pathName);
}

bool NSAddLibraryWithSearching(const char* pathName)
{
    return gDyld.apis->NSAddLibraryWithSearching(pathName);
}

const mach_header* NSAddImage(const char* image_name, uint32_t options)
{
    return gDyld.apis->NSAddImage(image_name, options);
}

bool _dyld_present()
{
    return true;
}

bool _dyld_launched_prebound()
{
    gDyld.apis->obsolete();
    return false;
}

bool _dyld_all_twolevel_modules_prebound()
{
    gDyld.apis->obsolete();
    return false;
}

bool _dyld_bind_fully_image_containing_address(const void* address)
{
    // in dyld4, everything is always fully bound
    return true;
}

bool _dyld_image_containing_address(const void* address)
{
    return gDyld.apis->_dyld_image_containing_address(address);
}

void _dyld_lookup_and_bind(const char* symbol_name, void** address, NSModule* module)
{
    gDyld.apis->_dyld_lookup_and_bind(symbol_name, address, module);
}

void _dyld_lookup_and_bind_with_hint(const char* symbol_name, const char* library_name_hint, void** address, NSModule* module)
{
    gDyld.apis->_dyld_lookup_and_bind_with_hint(symbol_name, library_name_hint, address, module);
}

void _dyld_lookup_and_bind_fully(const char* symbol_name, void** address, NSModule* module)
{
    gDyld.apis->_dyld_lookup_and_bind_fully(symbol_name, address, module);
}

const mach_header* _dyld_get_image_header_containing_address(const void* address)
{
    return gDyld.apis->dyld_image_header_containing_address(address);
}
#endif // TARGET_OS_OSX


//
// MARK: --- APIs Added macOS 10.6 ---
//
intptr_t  _dyld_get_image_slide(const mach_header* mh)
{
    return gDyld.apis->_dyld_get_image_slide(mh);
}

const char* dyld_image_path_containing_address(const void* addr)
{
    return gDyld.apis->dyld_image_path_containing_address(addr);
}

#if !__USING_SJLJ_EXCEPTIONS__
bool _dyld_find_unwind_sections(void* addr, dyld_unwind_sections* info)
{
    return gDyld.apis->_dyld_find_unwind_sections(addr, info);
}
#endif


//
// MARK: --- APIs added iOS 6, macOS 10.8 ---
//
uint32_t dyld_get_sdk_version(const mach_header* mh)
{
    return gDyld.apis->dyld_get_sdk_version(mh);
}

uint32_t dyld_get_min_os_version(const mach_header* mh)
{
    return gDyld.apis->dyld_get_min_os_version(mh);
}

uint32_t dyld_get_program_sdk_version()
{
    return gDyld.apis->dyld_get_program_sdk_version();
}

uint32_t dyld_get_program_min_os_version()
{
    return gDyld.apis->dyld_get_program_min_os_version();
}



//
// MARK: --- APIs added iOS 7, macOS 10.9 ---
//
bool dyld_process_is_restricted()
{
    return gDyld.apis->dyld_process_is_restricted();
}



//
// MARK: --- APIs added iOS 8, macOS 10.10 ---
//
bool dyld_shared_cache_some_image_overridden()
{
    return gDyld.apis->dyld_shared_cache_some_image_overridden();
}

void dyld_dynamic_interpose(const mach_header* mh, const dyld_interpose_tuple array[], size_t count)
{
    // <rdar://74287303> (Star 21A185 REG: Adobe Photoshop 2021 crash on launch)
    return;
}

void _tlv_atexit(void (*termFunc)(void* objAddr), void* objAddr)
{
    gDyld.apis->_tlv_atexit(termFunc, objAddr);
}

#if !TARGET_OS_DRIVERKIT
void _tlv_bootstrap()
{
    gDyld.apis->_tlv_bootstrap();
}
#endif

void _tlv_exit()
{
    gDyld.apis->_tlv_exit();
}


//
// MARK: --- APIs added iOS 9, macOS 10.11, watchOS 2.0 ---
//
int dyld_shared_cache_iterate_text(const uuid_t cacheUuid, void (^callback)(const dyld_shared_cache_dylib_text_info* info))
{
    return gDyld.apis->dyld_shared_cache_iterate_text(cacheUuid, callback);
}

const mach_header* dyld_image_header_containing_address(const void* addr)
{
    return gDyld.apis->dyld_image_header_containing_address(addr);
}

const char* dyld_shared_cache_file_path()
{
    return gDyld.apis->dyld_shared_cache_file_path();
}

#if TARGET_OS_WATCH
uint32_t  dyld_get_program_sdk_watch_os_version()
{
    return gDyld.apis->dyld_get_program_sdk_watch_os_version();
}

uint32_t  dyld_get_program_min_watch_os_version()
{
    return gDyld.apis->dyld_get_program_min_watch_os_version();
}
#endif // TARGET_OS_WATCH



//
// MARK: --- APIs added iOS 10, macOS 10.12, watchOS 3.0 ---
//
void _dyld_objc_notify_register(_dyld_objc_notify_mapped m, _dyld_objc_notify_init i, _dyld_objc_notify_unmapped u)
{
    gDyld.apis->_dyld_objc_notify_register(m, i, u);
}

bool _dyld_get_image_uuid(const mach_header* mh, uuid_t uuid)
{
    return gDyld.apis->_dyld_get_image_uuid(mh, uuid);
}

bool _dyld_get_shared_cache_uuid(uuid_t uuid)
{
    return gDyld.apis->_dyld_get_shared_cache_uuid(uuid);
}

bool _dyld_is_memory_immutable(const void* addr, size_t length)
{
    return gDyld.apis->_dyld_is_memory_immutable(addr, length);
}

int  dyld_shared_cache_find_iterate_text(const uuid_t cacheUuid, const char* extraSearchDirs[], void (^callback)(const dyld_shared_cache_dylib_text_info* info))
{
    return gDyld.apis->dyld_shared_cache_find_iterate_text(cacheUuid, extraSearchDirs, callback);
}



//
// MARK: --- APIs iOS 11, macOS 10.13, bridgeOS 2.0 ---
//
const void* _dyld_get_shared_cache_range(size_t* length)
{
    return gDyld.apis->_dyld_get_shared_cache_range(length);
}

#if TARGET_OS_BRIDGE
uint32_t dyld_get_program_sdk_bridge_os_version()
{
    return gDyld.apis->dyld_get_program_sdk_bridge_os_version();
}

uint32_t dyld_get_program_min_bridge_os_version()
{
    return gDyld.apis->dyld_get_program_min_bridge_os_version();
}
#endif // TARGET_OS_BRIDGE



//
// MARK: --- APIs iOS 12, macOS 10.14 ---
//
dyld_platform_t dyld_get_active_platform()
{
    return gDyld.apis->dyld_get_active_platform();
}

dyld_platform_t dyld_get_base_platform(dyld_platform_t platform)
{
    return gDyld.apis->dyld_get_base_platform(platform);
}

bool dyld_is_simulator_platform(dyld_platform_t platform)
{
    return gDyld.apis->dyld_is_simulator_platform(platform);
}

bool dyld_sdk_at_least(const mach_header* mh, dyld_build_version_t version)
{
    return gDyld.apis->dyld_sdk_at_least(mh, version);
}

bool dyld_minos_at_least(const mach_header* mh, dyld_build_version_t version)
{
    return gDyld.apis->dyld_minos_at_least(mh, version);
}

bool dyld_program_sdk_at_least(dyld_build_version_t version)
{
    return gDyld.apis->dyld_program_sdk_at_least(version);
}

bool dyld_program_minos_at_least(dyld_build_version_t version)
{
    return gDyld.apis->dyld_program_minos_at_least(version);
}

void dyld_get_image_versions(const mach_header* mh, void (^callback)(dyld_platform_t platform, uint32_t sdk_version, uint32_t min_version))
{
    gDyld.apis->dyld_get_image_versions(mh, callback);
}

void _dyld_images_for_addresses(unsigned count, const void* addresses[], dyld_image_uuid_offset infos[])
{
    gDyld.apis->_dyld_images_for_addresses(count, addresses, infos);
}

void _dyld_register_for_image_loads(void (*func)(const mach_header* mh, const char* path, bool unloadable))
{
    gDyld.apis->_dyld_register_for_image_loads(func);
}



//
// MARK: --- APIs added iOS 13, macOS 10.15 ---
//
void _dyld_atfork_prepare()
{
    gDyld.apis->_dyld_atfork_prepare();
}

void _dyld_atfork_parent()
{
    gDyld.apis->_dyld_atfork_parent();
}

bool dyld_need_closure(const char* execPath, const char* dataContainerRootDir)
{
    return gDyld.apis->dyld_need_closure(execPath, dataContainerRootDir);
}

bool dyld_has_inserted_or_interposing_libraries()
{
    return gDyld.apis->dyld_has_inserted_or_interposing_libraries();
}

bool _dyld_shared_cache_optimized()
{
    return gDyld.apis->_dyld_shared_cache_optimized();
}

bool _dyld_shared_cache_is_locally_built()
{
    return gDyld.apis->_dyld_shared_cache_is_locally_built();
}

void _dyld_register_for_bulk_image_loads(void (*func)(unsigned imageCount, const mach_header* mhs[], const char* paths[]))
{
    gDyld.apis->_dyld_register_for_bulk_image_loads(func);
}

void _dyld_register_driverkit_main(void (*mainFunc)(void))
{
    gDyld.apis->_dyld_register_driverkit_main(mainFunc);
}

void _dyld_missing_symbol_abort()
{
    gDyld.apis->_dyld_missing_symbol_abort();
}

const char* _dyld_get_objc_selector(const char* selName)
{
    return gDyld.apis->_dyld_get_objc_selector(selName);
}

void _dyld_for_each_objc_class(const char* className, void (^callback)(void* classPtr, bool isLoaded, bool* stop))
{
    gDyld.apis->_dyld_for_each_objc_class(className, callback);
}

void _dyld_for_each_objc_protocol(const char* protocolName, void (^callback)(void* protocolPtr, bool isLoaded, bool* stop))
{
    gDyld.apis->_dyld_for_each_objc_protocol(protocolName, callback);
}


//
// MARK: --- APIs added iOS 14, macOS 11 ---
//
uint32_t _dyld_launch_mode()
{
    return gDyld.apis->_dyld_launch_mode();
}

bool _dyld_is_objc_constant(DyldObjCConstantKind kind, const void* addr)
{
    return gDyld.apis->_dyld_is_objc_constant(kind, addr);
}

bool _dyld_has_fix_for_radar(const char* rdar)
{
    return gDyld.apis->_dyld_has_fix_for_radar(rdar);
}

const char* _dyld_shared_cache_real_path(const char* path)
{
    return gDyld.apis->_dyld_shared_cache_real_path(path);
}

#if !TARGET_OS_DRIVERKIT
bool _dyld_shared_cache_contains_path(const char* path)
{
    return gDyld.apis->_dyld_shared_cache_contains_path(path);
}

void* dlopen_from(const char* path, int mode, void* addressInCaller)
{
    return gDyld.apis->dlopen_from(path, mode, addressInCaller);
}

#if !__i386__
void* dlopen_audited(const char* path, int mode)
{
    return gDyld.apis->dlopen_audited(path, mode);
}
#endif // !__i386__
#endif // !TARGET_OS_DRIVERKIT

const struct mach_header* _dyld_get_prog_image_header()
{
    return gDyld.apis->_dyld_get_prog_image_header();
}



//
// MARK: --- APIs added iOS 15, macOS 12 ---
//
void _dyld_visit_objc_classes(void (^callback)(const void* classPtr))
{
    gDyld.apis->_dyld_visit_objc_classes(callback);
}

uint32_t _dyld_objc_class_count(void)
{
    return gDyld.apis->_dyld_objc_class_count();
}

bool _dyld_objc_uses_large_shared_cache(void)
{
    return gDyld.apis->_dyld_objc_uses_large_shared_cache();
}

struct _dyld_protocol_conformance_result _dyld_find_protocol_conformance(const void *protocolDescriptor,
                                                                         const void *metadataType,
                                                                         const void *typeDescriptor)
{
    return gDyld.apis->_dyld_find_protocol_conformance(protocolDescriptor, metadataType, typeDescriptor);
}

struct _dyld_protocol_conformance_result _dyld_find_foreign_type_protocol_conformance(const void *protocol,
                                                                                      const char *foreignTypeIdentityStart,
                                                                                      size_t foreignTypeIdentityLength)
{
    return gDyld.apis->_dyld_find_foreign_type_protocol_conformance(protocol, foreignTypeIdentityStart, foreignTypeIdentityLength);
}

uint32_t _dyld_swift_optimizations_version()
{
    return gDyld.apis->_dyld_swift_optimizations_version();
}

//
// MARK: --- crt data symbols ---
//
int          NXArgc = 0;
const char** NXArgv = NULL;
      char** environ = NULL;
const char*  __progname = NULL;

