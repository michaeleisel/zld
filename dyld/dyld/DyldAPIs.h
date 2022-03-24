/*
 * Copyright (c) 2020 Apple Inc. All rights reserved.
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

#ifndef DYLD_APIS_H
#define DYLD_APIS_H

#include "DyldRuntimeState.h"

namespace dyld4 {


class VIS_HIDDEN APIs : public RuntimeState
{
public:
#if BUILDING_DYLD
    static      APIs&                         bootstrap(const ProcessConfig& c, RuntimeLocks& locks);
#else
    static      APIs&                         bootstrap(const ProcessConfig& c);
#endif

    //
    // private call from libdyld.dylib into dyld to tell that libSystem.dylib is initialized
    //
    virtual     void                        _libdyld_initialize(const LibSystemHelpers* helpers);


    //
    // APIs from macOS 10.2
    //
    virtual     uint32_t                    _dyld_image_count();
    virtual     const mach_header*          _dyld_get_image_header(uint32_t index);
    virtual     intptr_t                    _dyld_get_image_vmaddr_slide(uint32_t index);
    virtual     const char*                 _dyld_get_image_name(uint32_t index);
    virtual     void                        _dyld_register_func_for_add_image(void (*func)(const mach_header* mh, intptr_t vmaddr_slide));
    virtual     void                        _dyld_register_func_for_remove_image(void (*func)(const mach_header* mh, intptr_t vmaddr_slide));
    virtual     int32_t                     NSVersionOfLinkTimeLibrary(const char* libraryName);
    virtual     int32_t                     NSVersionOfRunTimeLibrary(const char* libraryName);
    virtual     int                         _NSGetExecutablePath(char* buf, uint32_t* bufsize);
    virtual     void                        _dyld_fork_child();


    //
    // APIs from macOS 10.4
    //
    virtual     int                         dladdr(const void*, Dl_info* result);
    virtual     void*                       dlopen(const char* path, int mode);
    virtual     int                         dlclose(void* handle);
    virtual     char*                       dlerror();
    virtual     void*                       dlsym(void* handle, const char* symbol);
    virtual     bool                        dlopen_preflight(const char* path);


    //
    // APIs deprecated in macOS 10.5 and not on any other platform
    //
    virtual     NSObjectFileImageReturnCode NSCreateObjectFileImageFromFile(const char* pathName, NSObjectFileImage* objectFileImage);
    virtual     NSObjectFileImageReturnCode NSCreateObjectFileImageFromMemory(const void* address, size_t size, NSObjectFileImage* objectFileImage);
    virtual     bool                        NSDestroyObjectFileImage(NSObjectFileImage objectFileImage);
    virtual     bool                        NSIsSymbolDefinedInObjectFileImage(NSObjectFileImage objectFileImage, const char* symbolName);
    virtual     void*                       NSGetSectionDataInObjectFileImage(NSObjectFileImage objectFileImage, const char* segmentName, const char* sectionName, size_t* size);
    virtual     const char*                 NSNameOfModule(NSModule m);
    virtual     const char*                 NSLibraryNameForModule(NSModule m);
    virtual     NSModule                    NSLinkModule(NSObjectFileImage objectFileImage, const char* moduleName, uint32_t options);
    virtual     bool                        NSUnLinkModule(NSModule module, uint32_t options);
    virtual     bool                        NSIsSymbolNameDefined(const char* symbolName);
    virtual     bool                        NSIsSymbolNameDefinedWithHint(const char* symbolName, const char* libraryNameHint);
    virtual     bool                        NSIsSymbolNameDefinedInImage(const mach_header* image, const char* symbolName);
    virtual     NSSymbol                    NSLookupAndBindSymbol(const char* symbolName);
    virtual     NSSymbol                    NSLookupAndBindSymbolWithHint(const char* symbolName, const char* libraryNameHint);
    virtual     NSSymbol                    NSLookupSymbolInModule(NSModule module, const char* symbolName);
    virtual     NSSymbol                    NSLookupSymbolInImage(const mach_header* image, const char* symbolName, uint32_t options);
    virtual     void*                       NSAddressOfSymbol(NSSymbol symbol);
    virtual     NSModule                    NSModuleForSymbol(NSSymbol symbol);
    virtual     void                        NSLinkEditError(NSLinkEditErrors* c, int* errorNumber, const char** fileName, const char** errorString);
    virtual     bool                        NSAddLibrary(const char* pathName);
    virtual     bool                        NSAddLibraryWithSearching(const char* pathName);
    virtual     const mach_header*          NSAddImage(const char* image_name, uint32_t options);
    virtual     bool                        _dyld_image_containing_address(const void* address);
    virtual     void                        _dyld_lookup_and_bind(const char* symbol_name, void** address, NSModule* module);
    virtual     void                        _dyld_lookup_and_bind_with_hint(const char* symbol_name, const char* library_name_hint, void** address, NSModule* module);
    virtual     void                        _dyld_lookup_and_bind_fully(const char* symbol_name, void** address, NSModule* module);


    //
    // Added macOS 10.6
    //
    virtual     intptr_t                    _dyld_get_image_slide(const mach_header* mh);
    virtual     const char*                 dyld_image_path_containing_address(const void* addr);
#if !__USING_SJLJ_EXCEPTIONS__
    virtual     bool                        _dyld_find_unwind_sections(void* addr, dyld_unwind_sections* info);
#endif

    //
    // Added iOS 6, macOS 10.8
    //
    virtual     uint32_t                    dyld_get_sdk_version(const mach_header* mh);
    virtual     uint32_t                    dyld_get_min_os_version(const mach_header* mh);
    virtual     uint32_t                    dyld_get_program_sdk_version();
    virtual     uint32_t                    dyld_get_program_min_os_version();


    //
    // Added iOS 7, macOS 10.9
    //
    virtual     bool                        dyld_process_is_restricted();


    //
    // Added iOS 8, macOS 10.10
    //
    virtual     bool                        dyld_shared_cache_some_image_overridden();
    virtual     void                        _tlv_atexit(void (*termFunc)(void* objAddr), void* objAddr);
    virtual     void                        _tlv_bootstrap();
    virtual     void                        _tlv_exit();


    //
    // Added iOS 9, macOS 10.11, watchOS 2.0
    //
    virtual     int                         dyld_shared_cache_iterate_text(const uuid_t cacheUuid, void (^callback)(const dyld_shared_cache_dylib_text_info* info));
    virtual     const mach_header*          dyld_image_header_containing_address(const void* addr);
    virtual     const char*                 dyld_shared_cache_file_path();
    virtual     uint32_t                    dyld_get_program_sdk_watch_os_version();
    virtual     uint32_t                    dyld_get_program_min_watch_os_version();


    //
    // Added iOS 10, macOS 10.12, watchOS 3.0
    //
    virtual     void                        _dyld_objc_notify_register(_dyld_objc_notify_mapped, _dyld_objc_notify_init, _dyld_objc_notify_unmapped);
    virtual     bool                        _dyld_get_image_uuid(const mach_header* mh, uuid_t uuid);
    virtual     bool                        _dyld_get_shared_cache_uuid(uuid_t uuid);
    virtual     bool                        _dyld_is_memory_immutable(const void* addr, size_t length);
    virtual     int                         dyld_shared_cache_find_iterate_text(const uuid_t cacheUuid, const char* extraSearchDirs[], void (^callback)(const dyld_shared_cache_dylib_text_info* info));


    //
    // Added iOS 11, macOS 10.13, bridgeOS 2.0
    //
    virtual     const void*                 _dyld_get_shared_cache_range(size_t* length);
    virtual     uint32_t                    dyld_get_program_sdk_bridge_os_version();
    virtual     uint32_t                    dyld_get_program_min_bridge_os_version();


    //
    // Added iOS 12, macOS 10.14
    //
    virtual     dyld_platform_t             dyld_get_active_platform();
    virtual     dyld_platform_t             dyld_get_base_platform(dyld_platform_t platform);
    virtual     bool                        dyld_is_simulator_platform(dyld_platform_t platform);
    virtual     bool                        dyld_sdk_at_least(const mach_header* mh, dyld_build_version_t version);
    virtual     bool                        dyld_minos_at_least(const mach_header* mh, dyld_build_version_t version);
    virtual     bool                        dyld_program_sdk_at_least(dyld_build_version_t version);
    virtual     bool                        dyld_program_minos_at_least(dyld_build_version_t version);
    virtual     void                        dyld_get_image_versions(const mach_header* mh, void (^callback)(dyld_platform_t platform, uint32_t sdk_version, uint32_t min_version));
    virtual     void                        _dyld_images_for_addresses(unsigned count, const void* addresses[], dyld_image_uuid_offset infos[]);
    virtual     void                        _dyld_register_for_image_loads(void (*func)(const mach_header* mh, const char* path, bool unloadable));


    //
    // Added iOS 13, macOS 10.15
    //
    virtual     void                        _dyld_atfork_prepare();
    virtual     void                        _dyld_atfork_parent();
    virtual     bool                        dyld_need_closure(const char* execPath, const char* dataContainerRootDir);
    virtual     bool                        dyld_has_inserted_or_interposing_libraries(void);
    virtual     bool                        _dyld_shared_cache_optimized(void);
    virtual     bool                        _dyld_shared_cache_is_locally_built(void);
    virtual     void                        _dyld_register_for_bulk_image_loads(void (*func)(unsigned imageCount, const mach_header* mhs[], const char* paths[]));
    virtual     void                        _dyld_register_driverkit_main(void (*mainFunc)(void));
    virtual     void                        _dyld_missing_symbol_abort();
    virtual     const char*                 _dyld_get_objc_selector(const char* selName);
    virtual     void                        _dyld_for_each_objc_class(const char* className, void (^)(void* classPtr, bool isLoaded, bool* stop));
    virtual     void                        _dyld_for_each_objc_protocol(const char* protocolName, void (^)(void* protocolPtr, bool isLoaded, bool* stop));


    //
    // Added iOS 14, macOS 11
    //
    virtual     uint32_t                    _dyld_launch_mode();
    virtual     bool                        _dyld_is_objc_constant(DyldObjCConstantKind kind, const void* addr);
    virtual     bool                        _dyld_has_fix_for_radar(const char* rdar);
    virtual     const char*                 _dyld_shared_cache_real_path(const char* path);
    virtual     bool                        _dyld_shared_cache_contains_path(const char* path);
    virtual     void*                       dlopen_from(const char* path, int mode, void* addressInCaller);
#if !__i386__
    virtual     void *                      dlopen_audited(const char * path, int mode);
#endif
    virtual     const struct mach_header*   _dyld_get_prog_image_header();


    //
    // Added iOS 15, macOS 12
    //
    virtual     void                                obsolete() __attribute__((noreturn));
    virtual     void                                _dyld_visit_objc_classes(void (^callback)(const void* classPtr));
    virtual     uint32_t                            _dyld_objc_class_count(void);
    virtual     bool                                _dyld_objc_uses_large_shared_cache(void);
    virtual     _dyld_protocol_conformance_result   _dyld_find_protocol_conformance(const void *protocolDescriptor,
                                                                                    const void *metadataType,
                                                                                    const void *typeDescriptor) const;
    virtual     _dyld_protocol_conformance_result   _dyld_find_foreign_type_protocol_conformance(const void *protocol,
                                                                                                 const char *foreignTypeIdentityStart,
                                                                                                 size_t foreignTypeIdentityLength) const;
    virtual     uint32_t                            _dyld_swift_optimizations_version() const;
    virtual     void                                runAllInitializersForMain();
    virtual     void                                _dyld_before_fork_dlopen();
    virtual     void                                _dyld_after_fork_dlopen_parent();
    virtual     void                                _dyld_after_fork_dlopen_child();


private:
#if BUILDING_DYLD
                            APIs(const ProcessConfig& c, RuntimeLocks& locks, Allocator* alloc) : RuntimeState(c, locks, *alloc) {}
#else
                            APIs(const ProcessConfig& c, Allocator* alloc) : RuntimeState(c, *alloc) {}
#endif

    // internal helpers
    uint32_t                getSdkVersion(const mach_header* mh);
    dyld_build_version_t    mapFromVersionSet(dyld_build_version_t version);
    uint32_t                linkedDylibVersion(const dyld3::MachOFile* mf, const char* installname);
    uint32_t                deriveVersionFromDylibs(const dyld3::MachOFile* mf);
    void                    forEachPlatform(const dyld3::MachOFile* mf, void (^callback)(dyld_platform_t platform, uint32_t sdk_version, uint32_t min_version));
    uint32_t                basePlaform(uint32_t reqPlatform) const;
    bool                    findImageMappedAt(const void* addr, const MachOLoaded** ml, bool* neverUnloads = nullptr, const char** path = nullptr, const void** segAddr = nullptr, uint64_t* segSize = nullptr, uint8_t* segPerms = nullptr);
    void                    clearErrorString();
    void                    setErrorString(const char* format, ...) __attribute__((format(printf, 2, 3)));
    const Loader*           findImageContaining(void* addr);
    bool                    flatFindSymbol(const char* symbolName, void** symbolAddress, const mach_header** foundInImageAtLoadAddress);
    bool                    validLoader(const Loader* maybeLoader);
    void                    forEachImageVersion(const mach_header* mh, void (^callback)(dyld_platform_t platform, uint32_t sdk_version, uint32_t min_version));
};

} // namespace dyld4

#endif


