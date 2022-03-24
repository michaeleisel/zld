/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
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

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <libproc.h>
#include <mach/mach_time.h> // mach_absolute_time()
#include <mach/mach_init.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <libkern/OSAtomic.h>
#include <_simple.h>
#include <os/lock_private.h>
#include <Availability.h>
#include <System/sys/codesign.h>
#include <System/sys/csr.h>
#include <System/sys/reason.h>
#include <System/machine/cpu_capabilities.h>
#include <CrashReporterClient.h>
#if !TARGET_OS_SIMULATOR
    #include <libamfi.h>
#endif
#if __has_feature(ptrauth_calls)
    #include <ptrauth.h>
#endif

#include "StringUtils.h"
#include "dyld_process_info_internal.h"
#include "dyldSyscallInterface.h"
#include "MachOLoaded.h"
#include "DyldSharedCache.h"
#include "SharedCacheRuntime.h"
#include "Tracing.h"
#include "Loader.h"
#include "JustInTimeLoader.h"
#include "PrebuiltLoader.h"
#include "DebuggerSupport.h"
#include "DyldProcessConfig.h"
#include "DyldRuntimeState.h"
#include "DyldAPIs.h"

using dyld3::FatFile;
using dyld3::GradedArchs;
using dyld3::MachOAnalyzer;
using dyld3::MachOFile;
using dyld3::MachOLoaded;

extern "C" void mach_init();
extern "C" void __guard_setup(const char* apple[]);
extern "C" void _subsystem_init(const char* apple[]);

static const MachOAnalyzer* getDyldMH()
{
#if __LP64__
    extern const MachOAnalyzer __dso_handle;
    return &__dso_handle;
#else
    // on 32-bit arm, __dso_handle is access through a GOT slot.  Since rebasing has not happened yet, that value is incorrect.
    // instead we scan backwards from this function looking for mach_header
    uintptr_t p = (uintptr_t)&getDyldMH;
    p = p & (-0x1000);
    while ( *((uint32_t*)p) != MH_MAGIC ) {
        p -= 0x1000;
    }
    return (MachOAnalyzer*)p;
#endif
}

#if TARGET_OS_SIMULATOR
const dyld::SyscallHelpers* gSyscallHelpers = nullptr;
#endif

namespace dyld4 {

#if SUPPPORT_PRE_LC_MAIN
// this is defined in dyldStartup.s
extern void gotoAppStart(uintptr_t start, const KernelArgs* kernArgs) __attribute__((noreturn));
#endif

// no header because only called from assembly
extern void start(const KernelArgs* kernArgs);

#if TARGET_OS_OSX
static void* getProcessInfo()
{
    return gProcessInfo;
}

static void sim_vlog(const char* format, va_list list)
{
    // FIXME this should go through RuntimeState object
    _simple_vdprintf(STDERR_FILENO, format, list);
}

static char* getcwd_sans_malloc(char* buf, size_t size)
{
    SyscallDelegate syscall;
    if ( syscall.getCWD(buf) )
        return buf;
    return nullptr;
}

static char* realpath_sans_malloc(const char* file_name, char* resolved_name)
{
    SyscallDelegate syscall;
    if ( syscall.realpath(file_name, resolved_name) )
        return resolved_name;
    return nullptr;
}

static DIR* opendir_fake(const char*) {
    // <rdar://81126810> Allow old simulator binaries to call back opendir
    return nullptr;
}

// These are syscalls that the macOS dyld makes available to dyld_sim
static const dyld::SyscallHelpers sSysCalls = {
    16,
    // added in version 1
    &open,
    &close,
    &pread,
    &write,
    &mmap,
    &munmap,
    &madvise,
    &stat,
    &fcntl,
    &ioctl,
    &issetugid,
    &getcwd_sans_malloc,
    &realpath_sans_malloc,
    &vm_allocate,
    &vm_deallocate,
    &vm_protect,
    &sim_vlog,
    &sim_vlog,
    &pthread_mutex_lock,
    &pthread_mutex_unlock,
    &mach_thread_self,
    &mach_port_deallocate,
    &task_self_trap,
    &mach_timebase_info,
    &OSAtomicCompareAndSwapPtrBarrier,
    &OSMemoryBarrier,
    &getProcessInfo,
    &__error,
    &mach_absolute_time,
    // added in version 2
    &thread_switch,
    // added in version 3 (no longer used)
    &opendir_fake,
    nullptr, // &readdir_r,
    nullptr, // &closedir,
    // added in version 4
    &coresymbolication_load_notifier,
    &coresymbolication_unload_notifier,
    // Added in version 5
    &proc_regionfilename,
    &getpid,
    &mach_port_insert_right,
    &mach_port_allocate,
    &mach_msg_sim_interposed,
    // Added in version 6
    &abort_with_payload,
    // Added in version 7
    &task_register_dyld_image_infos,
    &task_unregister_dyld_image_infos,
    &task_get_dyld_image_infos,
    &task_register_dyld_shared_cache_image_info,
    &task_register_dyld_set_dyld_state,
    &task_register_dyld_get_process_state,
    // Added in version 8
    &task_info,
    &thread_info,
    &kdebug_is_enabled,
    &kdebug_trace,
    // Added in version 9
    &kdebug_trace_string,
    // Added in version 10
    &amfi_check_dyld_policy_self,
    // Added in version 11
    &notifyMonitoringDyldMain,
    &notifyMonitoringDyld,
    // Add in version 12
    &mach_msg_destroy,
    &mach_port_construct,
    &mach_port_destruct,
    // Add in version 13
    &fstat,
    &vm_copy,
    // Add in version 14
    &task_dyld_process_info_notify_get,
    // Add in version 15
    &fsgetpath,
    // Add in version 16
    &getattrlistbulk
};

__attribute__((noinline)) static MainFunc prepareSim(RuntimeState& state, const char* dyldSimPath)
{
    // open dyld_sim
    int fd = dyld3::open(dyldSimPath, O_RDONLY, 0);
    if ( fd == -1 )
        halt("dyld_sim file could not be opened");

    // get file size of dyld_sim
    struct stat sb;
    if ( fstat(fd, &sb) == -1 )
        halt("stat(dyld_sim) failed");

    // mmap whole file temporarily
    void* tempMapping = ::mmap(nullptr, (size_t)sb.st_size, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0);
    if ( tempMapping == MAP_FAILED )
        halt("mmap(dyld_sim) failed");

    // if fat file, pick matching slice
    uint64_t             fileOffset = 0;
    uint64_t             fileLength = sb.st_size;
    const FatFile*       ff         = (FatFile*)tempMapping;
    Diagnostics          diag;
    bool                 missingSlice;
    const MachOAnalyzer* sliceMapping = nullptr;
    const GradedArchs&   archs        = GradedArchs::forCurrentOS(state.config.process.mainExecutable, false);
    if ( ff->isFatFileWithSlice(diag, sb.st_size, archs, true, fileOffset, fileLength, missingSlice) ) {
        sliceMapping = (MachOAnalyzer*)((uint8_t*)tempMapping + fileOffset);
    }
    else if ( ((MachOFile*)tempMapping)->isMachO(diag, fileLength) ) {
        sliceMapping = (MachOAnalyzer*)tempMapping;
    }
    else {
        halt("dyld_sim is not compatible with the loaded process, likely due to architecture mismatch");
    }

    // validate load commands
    if ( !sliceMapping->validMachOForArchAndPlatform(diag, (size_t)fileLength, "dyld_sim", archs, state.config.process.platform, true) )
        halt(diag.errorMessage()); //"dyld_sim is malformed");

    // dyld_sim has to be code signed
    uint32_t codeSigFileOffset;
    uint32_t codeSigSize;
    if ( !sliceMapping->hasCodeSignature(codeSigFileOffset, codeSigSize) )
        halt("dyld_sim is not code signed");

    // register code signature with kernel before mmap()ing segments
    fsignatures_t siginfo;
    siginfo.fs_file_start = fileOffset;                       // start of mach-o slice in fat file
    siginfo.fs_blob_start = (void*)(long)(codeSigFileOffset); // start of code-signature in mach-o file
    siginfo.fs_blob_size  = codeSigSize;                      // size of code-signature
    int result            = fcntl(fd, F_ADDFILESIGS_FOR_DYLD_SIM, &siginfo);
    if ( result == -1 ) {
        halt("dyld_sim fcntl(F_ADDFILESIGS_FOR_DYLD_SIM) failed");
    }
    // file range covered by code signature must extend up to code signature itself
    if ( siginfo.fs_file_start < codeSigFileOffset )
        halt("dyld_sim code signature does not cover all of dyld_sim");

    // reserve space, then mmap each segment
    const uint64_t mappedSize                  = sliceMapping->mappedSize();
    uint64_t       dyldSimPreferredLoadAddress = sliceMapping->preferredLoadAddress();
    vm_address_t   dyldSimLoadAddress          = 0;
    if ( ::vm_allocate(mach_task_self(), &dyldSimLoadAddress, (vm_size_t)mappedSize, VM_FLAGS_ANYWHERE) != 0 )
        halt("dyld_sim cannot allocate space");
    __block const char* mappingStr = nullptr;
    sliceMapping->forEachSegment(^(const MachOAnalyzer::SegmentInfo& info, bool& stop) {
        uintptr_t requestedLoadAddress = (uintptr_t)(info.vmAddr - dyldSimPreferredLoadAddress + dyldSimLoadAddress);
        void*     segAddress           = ::mmap((void*)requestedLoadAddress, (size_t)info.fileSize, info.protections, MAP_FIXED | MAP_PRIVATE, fd, fileOffset + info.fileOffset);
        //state.log("dyld_sim %s mapped at %p\n", seg->segname, segAddress);
        if ( segAddress == (void*)(-1) ) {
            mappingStr = "dyld_sim mmap() of segment failed";
            stop       = true;
        }
        else if ( ((uintptr_t)segAddress < dyldSimLoadAddress) || ((uintptr_t)segAddress + info.fileSize > dyldSimLoadAddress + mappedSize) ) {
            mappingStr = "dyld_sim mmap() to wrong location";
            stop       = true;
        }
    });
    if ( mappingStr != nullptr )
        halt(mappingStr);
    ::close(fd);
    ::munmap(tempMapping, (size_t)sb.st_size);

    // walk newly mapped dyld_sim __TEXT load commands to find entry point
    uint64_t entryOffset;
    bool     usesCRT;
    if ( !((MachOAnalyzer*)dyldSimLoadAddress)->getEntry(entryOffset, usesCRT) )
        halt("dyld_sim entry not found");

    // notify debugger that dyld_sim is loaded
    dyld_image_info info;
    info.imageLoadAddress = (mach_header*)dyldSimLoadAddress;
    info.imageFilePath    = state.longTermAllocator.strdup(dyldSimPath);
    info.imageFileModDate = sb.st_mtime;
    addImagesToAllImages(state.longTermAllocator, 1, &info);
    gProcessInfo->notification(dyld_image_adding, 1, &info);

	// <rdar://problem/5077374> have host dyld detach macOS shared cache from process before jumping into dyld_sim
    dyld3::deallocateExistingSharedCache();
    gProcessInfo->processDetachedFromSharedRegion = true;
    gProcessInfo->sharedCacheSlide        = 0;
    gProcessInfo->sharedCacheBaseAddress  = 0;
    ::bzero(gProcessInfo->sharedCacheUUID,sizeof(uuid_t));

    //TODO: Remove once drop support for simulators older than iOS 15, tvOS 15, and watchOS 8
    // Old simulators do not correctly fill out the private cache fields in the all_image_info, so do it for them
    __block bool setSimulatorSharedCachePath = false;
    ((dyld3::MachOFile*)dyldSimLoadAddress)->forEachSupportedPlatform(^(dyld3::Platform platform, uint32_t minOS, uint32_t sdk) {
        switch ( platform ) {
            case dyld3::Platform::iOS:
            case dyld3::Platform::tvOS:
            case dyld3::Platform::iOS_simulator:
            case dyld3::Platform::tvOS_simulator:
                if ( minOS <= 0x000F0000 )  // iOS >15.0
                    setSimulatorSharedCachePath = true;
                break;
            case dyld3::Platform::watchOS:
            case dyld3::Platform::watchOS_simulator:
                if ( minOS <= 0x00080000 )  // watchOS 8.0
                    setSimulatorSharedCachePath = true;
                break;
            default: break;
        }
    });

    if (setSimulatorSharedCachePath) {
        struct stat cacheStatBuf;
        char cachePath[MAXPATHLEN];
        const char* cacheDir = state.config.process.environ("DYLD_SHARED_CACHE_DIR");
        if (cacheDir) {
            strlcpy(cachePath, cacheDir, MAXPATHLEN);
            strlcat(cachePath, "/dyld_sim_shared_cache_", MAXPATHLEN);
            strlcat(cachePath, ((dyld3::MachOFile*)dyldSimLoadAddress)->archName(), MAXPATHLEN);
            if (state.config.syscall.stat(cachePath, &cacheStatBuf) == 0) {
                gProcessInfo->sharedCacheFSID = cacheStatBuf.st_dev;
                gProcessInfo->sharedCacheFSObjID = cacheStatBuf.st_ino;
            }
        }
    }

    // jump into new simulator dyld
    typedef MainFunc (*sim_entry_proc_t)(int argc, const char* const argv[], const char* const envp[], const char* const apple[],
                                         const mach_header* mainExecutableMH, const mach_header* dyldMH, uintptr_t dyldSlide,
                                         const dyld::SyscallHelpers* vtable, uintptr_t* startGlue);
    sim_entry_proc_t newDyld = (sim_entry_proc_t)(dyldSimLoadAddress + entryOffset);
    uintptr_t        startGlue;
    return (*newDyld)(state.config.process.argc, state.config.process.argv, state.config.process.envp, state.config.process.apple,
                      state.config.process.mainExecutable, (mach_header*)dyldSimLoadAddress,
                      (uintptr_t)(dyldSimLoadAddress - dyldSimPreferredLoadAddress), &sSysCalls, &startGlue);
}
#endif // TARGET_OS_OSX

//
// If the DYLD_SKIP_MAIN environment is set to 1, dyld will return the
// address of this function instead of main() in the target program which
// __dyld_start jumps to. Useful for qualifying dyld itself.
//
static int fake_main(int argc, const char* const argv[], const char* const envp[], const char* const apple[])
{
    return 0;
}


//
// Load any dependent dylibs and bind all together.
// Returns address of main() in target.
//
__attribute__((noinline)) static MainFunc prepare(APIs& state, const MachOAnalyzer* dyldMH)
{
    gProcessInfo->terminationFlags = 0; // by default show backtrace in crash logs
    gProcessInfo->platform         = (uint32_t)state.config.process.platform;
    gProcessInfo->dyldPath         = state.config.process.dyldPath;

    uint64_t launchTraceID = 0;
    if ( dyld3::kdebug_trace_dyld_enabled(DBG_DYLD_TIMING_LAUNCH_EXECUTABLE) ) {
        launchTraceID = dyld3::kdebug_trace_dyld_duration_start(DBG_DYLD_TIMING_LAUNCH_EXECUTABLE, (uint64_t)state.config.process.mainExecutable, 0, 0);
    }

#if TARGET_OS_OSX
    const bool isSimulatorProgram = MachOFile::isSimulatorPlatform(state.config.process.platform);
    if ( const char* simPrefixPath = state.config.pathOverrides.simRootPath() ) {
        if ( isSimulatorProgram ) {
            char simDyldPath[PATH_MAX];
            strlcpy(simDyldPath, simPrefixPath, PATH_MAX);
            strlcat(simDyldPath, "/usr/lib/dyld_sim", PATH_MAX);
            return prepareSim(state, simDyldPath);
        }
        halt("DYLD_ROOT_PATH only allowed with simulator programs");
    }
    else if ( isSimulatorProgram ) {
        halt("DYLD_ROOT_PATH not set for simulator program");
    }
#endif

    // check if main executable is valid
    // FIXME: Enable the call to isValidMainExecutable below
    /*Diagnostics diag;
    bool validMainExec = state.config.process.mainExecutable->isValidMainExecutable(diag, state.config.process.mainExecutablePath, -1, *(state.config.process.archs), state.config.process.platform);
    if ( !validMainExec ) {
        state.log("%s in %s", diag.errorMessage(), state.config.process.mainExecutablePath);
        halt(diag.errorMessage());
    }*/

    // log env variables if asked
    if ( state.config.log.env ) {
        for (const char* const* p=state.config.process.envp; *p != nullptr; ++p) {
            state.log("%s\n", *p);
        }
    }

    // check for pre-built Loader
    state.initializeClosureMode();
    const PrebuiltLoaderSet* mainSet    = state.processPrebuiltLoaderSet();
    Loader*                  mainLoader = nullptr;
    if ( mainSet != nullptr ) {
        mainLoader = (Loader*)mainSet->atIndex(0);
    }
    if ( mainLoader == nullptr ) {
        // if no pre-built Loader, make a just-in-time one
        Diagnostics buildDiag;
        mainLoader = JustInTimeLoader::makeLaunchLoader(buildDiag, state, state.config.process.mainExecutable, state.config.process.mainExecutablePath);
        if ( buildDiag.hasError() ) {
            state.log("%s in %s\n", buildDiag.errorMessage(), state.config.process.mainExecutablePath);
            halt(buildDiag.errorMessage());
        }
    }
    state.setMainLoader(mainLoader);
    // start by just adding main executable to debuggers's known image list
    state.notifyDebuggerLoad(mainLoader);

    const bool needToWritePrebuiltLoaderSet = !mainLoader->isPrebuilt && (state.saveAppClosureFile() || state.failIfCouldBuildAppClosureFile());

    // <rdar://problem/10583252> Add dyld to uuidArray to enable symbolication of stackshots
    dyld_uuid_info dyldInfo;
    dyldInfo.imageLoadAddress = dyldMH;
    ((MachOFile*)dyldInfo.imageLoadAddress)->getUuid(dyldInfo.imageUUID);
    addNonSharedCacheImageUUID(state.longTermAllocator, dyldInfo);

    // load any inserted dylibs
    STACK_ALLOC_ARRAY(Loader*, topLevelLoaders, 16);
    topLevelLoaders.push_back(mainLoader);
    Loader::LoadChain   loadChainMain { nullptr, mainLoader };
    Loader::LoadOptions options;
    options.staticLinkage   = true;
    options.launching       = true;
    options.insertedDylib   = true;
    options.canBeDylib      = true;
    options.rpathStack      = &loadChainMain;
    state.config.pathOverrides.forEachInsertedDylib(^(const char* dylibPath, bool& stop) {
        Diagnostics insertDiag;
        if ( Loader* insertedDylib = (Loader*)Loader::getLoader(insertDiag, state, dylibPath, options) ) {
            topLevelLoaders.push_back(insertedDylib);
            state.notifyDebuggerLoad(insertedDylib);
            if ( insertedDylib->isPrebuilt )
                state.loaded.push_back(insertedDylib);
        }
        else if ( insertDiag.hasError() && !state.config.security.allowInsertFailures  ) {
            state.log("terminating because inserted dylib '%s' could not be loaded: %s\n", dylibPath, insertDiag.errorMessageCStr());
            halt(insertDiag.errorMessage());
        }
    });

    // move inserted libraries ahead of main executable in state.loaded, for correct flat namespace lookups
    if ( topLevelLoaders.count() != 1 ) {
        state.loaded.erase(state.loaded.begin());
        state.loaded.push_back(mainLoader);
    }

    // for recording files that must be missing
    __block MissingPaths missingPaths;
    auto missingLogger = ^(const char* mustBeMissingPath) {
        missingPaths.addPath(mustBeMissingPath);
    };

    // recursively load everything needed by main executable and inserted dylibs
    Diagnostics depsDiag;
    options.insertedDylib = false;
    if ( needToWritePrebuiltLoaderSet )
        options.pathNotFoundHandler = missingLogger;
    for ( Loader* ldr : topLevelLoaders ) {
        ldr->loadDependents(depsDiag, state, options);
        if ( depsDiag.hasError() ) {
            //state.log("%s loading dependents of %s\n", depsDiag.errorMessage(), ldr->path());
            // let debugger/crashreporter know about dylibs we were able to load
            uintptr_t topCount = topLevelLoaders.count();
            STACK_ALLOC_ARRAY(const Loader*, newLoaders, state.loaded.size() - topCount);
            for (size_t i = topCount; i != state.loaded.size(); ++i)
                newLoaders.push_back(state.loaded[i]);
            state.notifyDebuggerLoad(newLoaders);
            gProcessInfo->terminationFlags = 1; // don't show back trace, because nothing interesting
            halt(depsDiag.errorMessage());
        }
    }

    uintptr_t topCount = topLevelLoaders.count();
    {
        STACK_ALLOC_ARRAY(const Loader*, newLoaders, state.loaded.size());
        for (const Loader* ldr : state.loaded)
            newLoaders.push_back(ldr);

        // notify debugger about all loaded images after the main executable
        state.notifyDebuggerLoad(newLoaders.subArray(topCount, newLoaders.count() - topCount));

        // notify kernel about any dtrace static user probes
        state.notifyDtrace(newLoaders);
    }

    // add to permanent ranges
    STACK_ALLOC_ARRAY(const Loader*, nonCacheNeverUnloadLoaders, state.loaded.size());
    for (const Loader* ldr : state.loaded) {
        if ( !ldr->dylibInDyldCache )
            nonCacheNeverUnloadLoaders.push_back(ldr);
    }
    state.addPermanentRanges(nonCacheNeverUnloadLoaders);

    // proactive weakDefMap means we build the weakDefMap before doing any binding
    if ( state.config.process.proactivelyUseWeakDefMap ) {
        state.weakDefMap = new (state.longTermAllocator.malloc(sizeof(WeakDefMap))) WeakDefMap();
        STACK_ALLOC_ARRAY(const Loader*, allLoaders, state.loaded.size());
        for (const Loader* ldr : state.loaded)
            allLoaders.push_back(ldr);
        Loader::addWeakDefsToMap(state, allLoaders);
    }

    // check for interposing tuples before doing fixups
    state.buildInterposingTables();

    // do fixups
    {
        dyld3::ScopedTimer(DBG_DYLD_TIMING_APPLY_FIXUPS, 0, 0, 0);
        // just in case we need to patch the case
        DyldCacheDataConstLazyScopedWriter  cacheDataConst(state);

        // The C++ spec says main executables can define non-weak functions which override weak-defs in dylibs
        // This happens automatically for anything bound at launch, but the dyld cache is pre-bound so we need
        // to patch any binds that are overridden by this non-weak in the main executable.
        // Note on macOS we also allow dylibs to have non-weak overrides of weak-defs
        if ( !mainLoader->isPrebuilt )
            JustInTimeLoader::handleStrongWeakDefOverrides(state, cacheDataConst);

        for ( const Loader* ldr : state.loaded ) {
            Diagnostics fixupDiag;
            ldr->applyFixups(fixupDiag, state, cacheDataConst, true);
            if ( fixupDiag.hasError() ) {
                halt(fixupDiag.errorMessage());
            }
        }
    }

    // if there is interposing, the apply interpose tuples to the dyld cache
    if ( !state.interposingTuplesAll.empty() ) {
        Loader::applyInterposingToDyldCache(state);
    }

    // if mainLoader is prebuilt, there may be overrides of weak-defs in the dyld cache
    if ( mainLoader->isPrebuilt ) {
        DyldCacheDataConstLazyScopedWriter  dataConstWriter(state);
        DyldCacheDataConstLazyScopedWriter* dataConstWriterPtr = &dataConstWriter; // work around to make accessible in cacheWeakDefFixup
        state.processPrebuiltLoaderSet()->forEachCachePatch(^(const PrebuiltLoaderSet::CachePatch& patch) {
            uintptr_t newImpl = (uintptr_t)patch.patchTo.value(state);
            state.config.dyldCache.addr->forEachPatchableUseOfExport(patch.cacheDylibIndex, patch.cacheDylibVMOffset,
                                                                     ^(uint64_t cacheVMOffset,
                                                                       dyld3::MachOLoaded::PointerMetaData pmd, uint64_t addend) {
                uintptr_t* loc      = (uintptr_t*)(((uint8_t*)state.config.dyldCache.addr) + cacheVMOffset);
                uintptr_t  newValue = newImpl + (uintptr_t)addend;
#if __has_feature(ptrauth_calls)
                if ( pmd.authenticated )
                    newValue = MachOLoaded::ChainedFixupPointerOnDisk::Arm64e::signPointer(newValue, loc, pmd.usesAddrDiversity, pmd.diversity, pmd.key);
#endif
                // ignore duplicate patch entries
                if ( *loc != newValue ) {
                    dataConstWriterPtr->makeWriteable();
                    if ( state.config.log.fixups )
                        state.log("cache patch: %p = 0x%0lX\n", loc, newValue);
                    *loc = newValue;
                }
            });
        });
    }

    // call kdebug trace for each image
#if !TARGET_OS_SIMULATOR
    if ( kdebug_is_enabled(KDBG_CODE(DBG_DYLD, DBG_DYLD_UUID, DBG_DYLD_UUID_MAP_A)) ) {
        // add trace for dyld itself
        uuid_t dyldUuid;
        dyldMH->getUuid(dyldUuid);
        struct stat        stat_buf;
        fsid_t             dyldFsid    = { { 0, 0 } };
        fsobj_id_t         dyldFfsobjid = { 0, 0 };
        if ( dyld3::stat(state.config.process.dyldPath, &stat_buf) == 0 ) {
            dyldFfsobjid  = *(fsobj_id_t*)&stat_buf.st_ino;
            dyldFsid      = { { stat_buf.st_dev, 0 } };
        }
        kdebug_trace_dyld_image(DBG_DYLD_UUID_MAP_A, state.config.process.dyldPath, &dyldUuid, dyldFfsobjid, dyldFsid, dyldMH);

        // add trace for each image loaded
        for ( const Loader* ldr :  state.loaded ) {
            const MachOLoaded* ml = ldr->loadAddress(state);
            fsid_t             fsid    = { { 0, 0 } };
            fsobj_id_t         fsobjid = { 0, 0 };
            if ( !ldr->dylibInDyldCache && (dyld3::stat(ldr->path(), &stat_buf) == 0) ) { //FIXME Loader knows inode
                fsobjid = *(fsobj_id_t*)&stat_buf.st_ino;
                fsid    = { { stat_buf.st_dev, 0 } };
            }
            uuid_t uuid;
            ml->getUuid(uuid);
            kdebug_trace_dyld_image(DBG_DYLD_UUID_MAP_A, ldr->path(), &uuid, fsobjid, fsid, ml);
        }
    }
#endif

    // notify any other processing inspecting this one
    // notify any processes tracking loads in this process
    STACK_ALLOC_ARRAY(const char*, pathsBuffer, state.loaded.size());
    STACK_ALLOC_ARRAY(const mach_header*, mhBuffer, state.loaded.size());
    for ( const Loader* ldr :  state.loaded ) {
        pathsBuffer.push_back(ldr->path());
        mhBuffer.push_back(ldr->loadAddress(state));
    }

    notifyMonitoringDyld(false, (unsigned int)state.loaded.size(), &mhBuffer[0], &pathsBuffer[0]);

    // wire up libdyld.dylib to dyld
    LibdyldDyld4Section* libdyld4Section = nullptr;
    if ( state.libdyldLoader != nullptr ) {
        const MachOLoaded* libdyldML = state.libdyldLoader->loadAddress(state);
        uint64_t           sectSize;
        libdyld4Section = (LibdyldDyld4Section*)libdyldML->findSectionContent("__DATA", "__dyld4", sectSize, true);
#if __has_feature(ptrauth_calls)
        if ( libdyld4Section == nullptr )
            libdyld4Section = (LibdyldDyld4Section*)libdyldML->findSectionContent("__AUTH", "__dyld4", sectSize, true);
#endif
        if ( libdyld4Section != nullptr ) {
            // set pointer to global APIs object
            libdyld4Section->apis = &state;
            // set the pointer to dyld_all_image_infos
            libdyld4Section->allImageInfos = gProcessInfo;
            // program vars (e.g. environ) are usually defined in libdyld.dylib (but might be defined in main excutable for old macOS binaries)
            // remember location of progams vars so libc can sync them
            state.vars                 = &libdyld4Section->defaultVars;
            state.vars->mh             = state.config.process.mainExecutable;
            *state.vars->NXArgcPtr     = state.config.process.argc;
            *state.vars->NXArgvPtr     = (const char**)state.config.process.argv;
            *state.vars->environPtr    = (const char**)state.config.process.envp;
            *state.vars->__prognamePtr = state.config.process.progname;
        }
        else {
            halt("compatible libdyld.dylib not found");
        }
    }
    else {
        halt("libdyld.dylib not found");
    }
    if ( state.libSystemLoader == nullptr )
        halt("program does not link with libSystem.B.dylib");

#if !TARGET_OS_SIMULATOR
    // if launched with JustInTimeLoader, may need to serialize it
    if ( needToWritePrebuiltLoaderSet ) {
        dyld3::ScopedTimer timer(DBG_DYLD_TIMING_BUILD_CLOSURE, 0, 0, 0);
        if ( state.config.log.loaders )
            state.log("building PrebuiltLoaderSet for main executable\n");
        Diagnostics              prebuiltDiag;
        const PrebuiltLoaderSet* prebuiltAppSet = PrebuiltLoaderSet::makeLaunchSet(prebuiltDiag, state, missingPaths);
        if ( (prebuiltAppSet != nullptr) && prebuiltDiag.noError() ) {
            if ( state.failIfCouldBuildAppClosureFile() )
                halt("dyld: PrebuiltLoaderSet expected but not found");
            // save PrebuiltLoaderSet to disk for use by next launch, continue running with JustInTimeLoaders
            if ( state.saveAppPrebuiltLoaderSet(prebuiltAppSet) )
                state.setSavedPrebuiltLoaderSet();
            prebuiltAppSet->deallocate();
            timer.setData4(dyld3::DyldTimingBuildClosure::LaunchClosure_Built);
        }
        else if ( state.config.log.loaders ) {
            state.log("could not build PrebuiltLoaderSet: %s\n", prebuiltDiag.errorMessage());
        }
    }
#endif

#if SUPPPORT_PRE_LC_MAIN
    uint32_t                progVarsOffset;
    dyld3::DyldLookFunc*    dyldLookupFuncAddr = nullptr;
    bool                    crtRunsInitializers = false;
    if ( state.config.process.mainExecutable->hasProgramVars(progVarsOffset, crtRunsInitializers, dyldLookupFuncAddr) ) {
        // this is old macOS app which has its own NXArgv, etc global variables.  We need to use them.
        ProgramVars* varsInApp    = (ProgramVars*)(((uint8_t*)state.config.process.mainExecutable) + progVarsOffset);
        varsInApp->mh             = state.config.process.mainExecutable;
        *varsInApp->NXArgcPtr     = state.config.process.argc;
        *varsInApp->NXArgvPtr     = (const char**)state.config.process.argv;
        *varsInApp->environPtr    = (const char**)state.config.process.envp;
        *varsInApp->__prognamePtr = state.config.process.progname;
        state.vars                = varsInApp;
    }
    if ( dyldLookupFuncAddr ) {
        if ( libdyld4Section != nullptr ) {
            *dyldLookupFuncAddr = (dyld3::DyldLookFunc)libdyld4Section->dyldLookupFuncAddr;
        } else {
            halt("compatible libdyld.dylib not found");
        }
    }

    if ( !crtRunsInitializers )
        state.runAllInitializersForMain();

#else

    // run all initializers
    state.runAllInitializersForMain();

#endif // SUPPPORT_PRE_LC_MAIN



    // notify we are about to call main
    notifyMonitoringDyldMain();
    if ( dyld3::kdebug_trace_dyld_enabled(DBG_DYLD_TIMING_LAUNCH_EXECUTABLE) ) {
        dyld3::kdebug_trace_dyld_duration_end(launchTraceID, DBG_DYLD_TIMING_LAUNCH_EXECUTABLE, 0, 0, 4);
    }
    ARIADNEDBG_CODE(220, 1);

    MainFunc result;
    if ( state.config.security.skipMain ) {
        return &fake_main;
    }
    else if ( state.config.process.platform == dyld3::Platform::driverKit ) {
        result = state.mainFunc();
        if ( result == 0 )
            halt("DriverKit main entry point not set");
#if __has_feature(ptrauth_calls)
        // HACK: DriverKit signs the pointer with a diversity different than dyld expects when calling the pointer.
        result = (MainFunc)__builtin_ptrauth_strip((void*)result, ptrauth_key_function_pointer);
        result = (MainFunc)__builtin_ptrauth_sign_unauthenticated((void*)result, 0, 0);
#endif
    }
    else {
        // find entry point for main executable
        uint64_t entryOffset;
        bool     usesCRT;
        if ( !state.config.process.mainExecutable->getEntry(entryOffset, usesCRT) )
            halt("main executable has no entry point");
        result = (MainFunc)((uintptr_t)state.config.process.mainExecutable + entryOffset);
        if ( usesCRT ) {
            // main executable uses LC_UNIXTHREAD, dyld needs to cut back kernel arg stack and jump to "start"
#if SUPPPORT_PRE_LC_MAIN
            // backsolve for KernelArgs (original stack entry point in _dyld_start)
            const KernelArgs* kernArgs = (KernelArgs*)(&state.config.process.argv[-2]);
            gotoAppStart((uintptr_t)result, kernArgs);
#else
            halt("main executable is missing LC_MAIN");
#endif
        }
#if __has_feature(ptrauth_calls)
        result = (MainFunc)__builtin_ptrauth_sign_unauthenticated((void*)result, 0, 0);
#endif
    }

    return result;
}


// SyscallDelegate object which is held onto by config object for life of process
SyscallDelegate sSyscallDelegate;

// non-obvious:  we want a ProcessConfig object that is read-only and lasts life of process.
// We do that by allocating a buffer here (sConfigBuffer) and use placement new to construct
// a ProcessConfig into it, then we set __DATA_CONST to be read-only.
uint8_t         sConfigBuffer[sizeof(ProcessConfig)] __attribute__((section("__DATA_CONST,__const")));


#if SUPPPORT_PRE_LC_MAIN
// old macOS binaries need stack reset, so RuntimeLocks cannot be stack allocated
static RuntimeLocks sLocks;
#endif


//
// Entry point for dyld.  The kernel loads dyld and jumps to __dyld_start which
// sets up some registers and call this function.
//
// Note: this function never returns, it calls exit().  Therefore stack protectors
// are useless, since the epilog is never executed.  Marking the fucntion no-return
// disable the stack protector.  The stack protector was also causing problems
// with armv7k codegen since it access the random value through a GOT slot in the
// prolog, but dyld is not rebased yet.
//
void start(const KernelArgs* kernArgs) __attribute__((noreturn)) __asm("start");
void start(const KernelArgs* kernArgs)
{
    // Emit kdebug tracepoint to indicate dyld bootstrap has started <rdar://46878536>
    // Note: this is called before dyld is rebased, so kdebug_trace_dyld_marker() cannot use any global variables
    dyld3::kdebug_trace_dyld_marker(DBG_DYLD_TIMING_BOOTSTRAP_START, 0, 0, 0, 0);

    // walk all fixups chains and rebase dyld
    // Note: withChainStarts() and fixupAllChainedFixups() cannot use any static DATA pointers as they are not rebased yet
    const MachOAnalyzer* dyldMA = getDyldMH();
    assert(dyldMA->hasChainedFixups());
    uintptr_t           slide = (long)dyldMA; // all fixup chain based images have a base address of zero, so slide == load address
    __block Diagnostics diag;
    dyldMA->withChainStarts(diag, 0, ^(const dyld_chained_starts_in_image* starts) {
        dyldMA->fixupAllChainedFixups(diag, starts, slide, dyld3::Array<const void*>(), nullptr);
    });
    diag.assertNoError();

    // Now, we can call functions that use DATA
    mach_init();

    // set up random value for stack canary
    __guard_setup(kernArgs->findApple());

    // setup so that open_with_subsystem() works
    _subsystem_init(kernArgs->findApple());

    // use placement new to construct ProcessConfig object in __DATA_CONST, before it is made read-only
    ProcessConfig& config = *new ((ProcessConfig*)sConfigBuffer) ProcessConfig(kernArgs, sSyscallDelegate);

    // make __DATA_CONST read-only (kernel maps it r/w)
    dyldMA->forEachSegment(^(const MachOAnalyzer::SegmentInfo& segInfo, bool& stop) {
        if ( segInfo.readOnlyData ) {
            const uint8_t* start = (uint8_t*)(segInfo.vmAddr + slide);
            size_t         size  = (size_t)segInfo.vmSize;
            sSyscallDelegate.mprotect((void*)start, size, PROT_READ);
        }
    });

#if !SUPPPORT_PRE_LC_MAIN
    // stack allocate RuntimeLocks. They cannot be in the Allocator pool which is usually read-only
    RuntimeLocks  sLocks;
#endif

    // create Allocator and APIs/RuntimeState object in that allocator
    APIs& state = APIs::bootstrap(config, sLocks);

    // load all dependents of program and bind them together
    MainFunc appMain = prepare(state, dyldMA);

    // now make all dyld Allocated data structures read-only
    state.decWritable();

    // call main() and if it returns, call exit() with the result
    // Note: this is organized so that a backtrace in a program's main thread shows just "start" below "main"
    int result = appMain(state.config.process.argc, state.config.process.argv, state.config.process.envp, state.config.process.apple);

    // if we got here, main() returned (as opposed to program calling exit())
#if TARGET_OS_OSX
    // <rdar://74518676> libSystemHelpers is not set up for simulators, so directly call _exit()
    if ( MachOFile::isSimulatorPlatform(state.config.process.platform) )
        _exit(result);
#endif
    state.libSystemHelpers->exit(result);
}

} // namespace

#if TARGET_OS_SIMULATOR
using namespace dyld4;

static RuntimeLocks sLocks;

// glue to handle if main() in simulator program returns
// if _dyld_sim_prepare() returned main() then main() would return
// to the host dyld, which would be unable to run termination functions
// (e.g atexit()) in the simulator environment.  So instead, we wrap
// main() in start_sim() which can call simualtors exit() is main returns.
static APIs*    sAPIsForExit = nullptr;
static MainFunc sRealMain = nullptr;
static int start_sim(int argc, const char* const argv[], const char* const envp[], const char* const apple[]) __asm("start_sim");
static int start_sim(int argc, const char* const argv[], const char* const envp[], const char* const apple[])
{
    int result = sRealMain(argc, argv, envp, apple);
    sAPIsForExit->libSystemHelpers->exit(result);
    return 0;
}

extern "C" MainFunc _dyld_sim_prepare(int argc, const char* argv[], const char* envp[], const char* apple[],
                                 const mach_header* mainExecutableMH, const MachOAnalyzer* dyldMA, uintptr_t dyldSlide,
                                 const dyld::SyscallHelpers*, uintptr_t* startGlue);

MainFunc _dyld_sim_prepare(int argc, const char* argv[], const char* envp[], const char* apple[],
                      const mach_header* mainExecutableMH, const MachOAnalyzer* dyldMA, uintptr_t dyldSimSlide,
                      const dyld::SyscallHelpers* sc, uintptr_t* startGlue)
{
    // walk all fixups chains and rebase dyld_sim
    // Note: withChainStarts() and fixupAllChainedFixups() cannot use any static DATA pointers as they are not rebased yet
    assert(dyldMA->hasChainedFixups());
    uintptr_t           slide = (long)dyldMA; // all fixup chain based images have a base address of zero, so slide == load address
    __block Diagnostics diag;
    dyldMA->withChainStarts(diag, 0, ^(const dyld_chained_starts_in_image* starts) {
        dyldMA->fixupAllChainedFixups(diag, starts, slide, dyld3::Array<const void*>(), nullptr);
    });
    diag.assertNoError();

    // save table of syscall pointers
    gSyscallHelpers = sc;

    // Now, we can call functions that use DATA
    mach_init();

    // set up random value for stack canary
    __guard_setup(apple);

    // setup gProcessInfo to point to host dyld's struct
    gProcessInfo = (struct dyld_all_image_infos*)(sc->getProcessInfo());

    // back solve for KernelArgs because host dyld does not pass it
    KernelArgs* kernArgs = (KernelArgs*)(((uint8_t*)argv) - 2 * sizeof(void*));
    // before dyld4, the main executable mach_header was removed from the stack
    // so we need to force it back to allow KernelArgs to work like non-simulator processes
    // FIXME: remove when sims only run on dyld4 based macOS hosts
    kernArgs->mainExecutable = (MachOAnalyzer*)mainExecutableMH;

    // use placement new to construct ProcessConfig object in __DATA_CONST, before it is made read-only
    ProcessConfig& config = *new ((ProcessConfig*)sConfigBuffer) ProcessConfig(kernArgs, sSyscallDelegate);

    // make __DATA_CONST read-only (kernel maps it r/w)
    dyldMA->forEachSegment(^(const MachOAnalyzer::SegmentInfo& segInfo, bool& stop) {
        if ( segInfo.readOnlyData ) {
            const uint8_t* start = (uint8_t*)(segInfo.vmAddr + slide);
            size_t         size  = (size_t)segInfo.vmSize;
            sSyscallDelegate.mprotect((void*)start, size, PROT_READ);
        }
    });

    // create Allocator and APIs/RuntimeState object in that allocator
    APIs& state = APIs::bootstrap(config, sLocks);

    // now that allocator is up, we can update image list
    syncProcessInfo(state.longTermAllocator);

    // load all dependents of program and bind them together, then return address of main()
    MainFunc result = prepare(state, dyldMA);

    // now make all dyld Allocated data structures read-only
    state.decWritable();

    // return fake main, which calls real main() then simulator exit()
    *startGlue   = 1;  // means result is pointer to main(), as opposed to crt1.o entry
    sRealMain    = result;
    sAPIsForExit = &state;
    return &start_sim;
}
#endif // TARGET_OS_SIMULATOR
