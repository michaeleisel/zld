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

#include <TargetConditionals.h>
#include <_simple.h>
#include <stdint.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <System/sys/reason.h>
#include <kern/kcdata.h>
#include <libkern/OSAtomic.h>

// atexit header is missing C++ guards
extern "C" {
    #include <System/atexit.h>
}

// no libc header for send() syscall interface
extern "C" ssize_t __sendto(int, const void*, size_t, int, const struct sockaddr*, socklen_t);

#if TARGET_OS_SIMULATOR
enum
{
    AMFI_DYLD_INPUT_PROC_IN_SIMULATOR = (1 << 0),
};
enum amfi_dyld_policy_output_flag_set
{
    AMFI_DYLD_OUTPUT_ALLOW_AT_PATH                  = (1 << 0),
    AMFI_DYLD_OUTPUT_ALLOW_PATH_VARS                = (1 << 1),
    AMFI_DYLD_OUTPUT_ALLOW_CUSTOM_SHARED_CACHE      = (1 << 2),
    AMFI_DYLD_OUTPUT_ALLOW_FALLBACK_PATHS           = (1 << 3),
    AMFI_DYLD_OUTPUT_ALLOW_PRINT_VARS               = (1 << 4),
    AMFI_DYLD_OUTPUT_ALLOW_FAILED_LIBRARY_INSERTION = (1 << 5),
};
extern "C" int amfi_check_dyld_policy_self(uint64_t input_flags, uint64_t* output_flags);
#else
    #include <libamfi.h>
#endif

#include "MachOLoaded.h"
#include "DyldSharedCache.h"
#include "SharedCacheRuntime.h"
#include "Tracing.h"
#include "FileUtils.h"
#include "Loader.h"
#include "PrebuiltLoader.h"
#include "DyldRuntimeState.h"
#include "DebuggerSupport.h"
#include "DyldProcessConfig.h"
#include "RosettaSupport.h"
#include "Vector.h"

// implemented in assembly
extern "C" void* tlv_get_addr(MachOAnalyzer::TLV_Thunk*);

using dyld3::MachOAnalyzer;
using dyld3::MachOFile;
using dyld3::Platform;

extern "C" {
// historically crash reporter look for this symbol named "error_string" in dyld, but that may not be needed anymore
char error_string[1024] = "dyld: launch, loading dependent libraries";
};

#define DYLD_CLOSURE_XATTR_NAME "com.apple.dyld"

extern "C" const dyld3::MachOLoaded __dso_handle; // mach_header of dyld itself

static bool hexCharToByte(const char hexByte, uint8_t& value)
{
    if ( hexByte >= '0' && hexByte <= '9' ) {
        value = hexByte - '0';
        return true;
    }
    else if ( hexByte >= 'A' && hexByte <= 'F' ) {
        value = hexByte - 'A' + 10;
        return true;
    }
    else if ( hexByte >= 'a' && hexByte <= 'f' ) {
        value = hexByte - 'a' + 10;
        return true;
    }

    return false;
}

static bool hexStringToBytes(const char* hexString, uint8_t buffer[], unsigned bufferMaxSize, unsigned& bufferLenUsed)
{
    bufferLenUsed = 0;
    bool high     = true;
    for ( const char* s = hexString; *s != '\0'; ++s ) {
        if ( bufferLenUsed > bufferMaxSize )
            return false;
        uint8_t value;
        if ( !hexCharToByte(*s, value) )
            return false;
        if ( high )
            buffer[bufferLenUsed] = value << 4;
        else
            buffer[bufferLenUsed++] |= value;
        high = !high;
    }
    return true;
}

namespace dyld4 {

void RuntimeState::withLoadersReadLock(void (^work)())
{
#if BUILDING_DYLD
    if ( this->libSystemHelpers != nullptr ) {
        this->libSystemHelpers->os_unfair_recursive_lock_lock_with_options(&(_locks.loadersLock), OS_UNFAIR_LOCK_NONE);
        work();
        this->libSystemHelpers->os_unfair_recursive_lock_unlock(&_locks.loadersLock);
    }
    else
#endif
    {
        work();
    }
}

void RuntimeState::withLoadersWriteLock(void (^work)())
{
#if BUILDING_DYLD
    if ( this->libSystemHelpers != nullptr ) {
        this->libSystemHelpers->os_unfair_recursive_lock_lock_with_options(&_locks.loadersLock, OS_UNFAIR_LOCK_NONE);
        this->incWritable();
        work();
        this->decWritable();
        this->libSystemHelpers->os_unfair_recursive_lock_unlock(&_locks.loadersLock);
    }
    else
#endif
    {
        work();
    }
}

void RuntimeState::incWritable()
{
#if BUILDING_DYLD
    // FIXME: move inc/decWritable() into Allocator to replace writeProtect()
    pthread_mutex_lock(&_locks.writableLock);
        _locks.writeableCount += 1;
        if ( _locks.writeableCount == 1 )
            longTermAllocator.writeProtect(false);
    pthread_mutex_unlock(&_locks.writableLock);
#endif
}

void RuntimeState::decWritable()
{
#if BUILDING_DYLD
    pthread_mutex_lock(&_locks.writableLock);
        _locks.writeableCount -= 1;
        if ( _locks.writeableCount == 0 )
            longTermAllocator.writeProtect(true);
    pthread_mutex_unlock(&_locks.writableLock);
#endif
}

uint8_t* RuntimeState::appState(uint16_t index)
{
    assert(_processPrebuiltLoaderSet != nullptr);
    assert(index < _processPrebuiltLoaderSet->loaderCount());
    return &_processDylibStateArray[index];
}

const MachOLoaded* RuntimeState::appLoadAddress(uint16_t index)
{
    assert(_processPrebuiltLoaderSet != nullptr);
    assert(index < _processPrebuiltLoaderSet->loaderCount());
    return _processLoadedAddressArray[index];
}

void RuntimeState::setAppLoadAddress(uint16_t index, const MachOLoaded* ml)
{
    assert(_processPrebuiltLoaderSet != nullptr);
    assert(index < _processPrebuiltLoaderSet->loaderCount());
    _processLoadedAddressArray[index] = ml;
}

uint8_t* RuntimeState::cachedDylibState(uint16_t index)
{
    assert(index < this->config.dyldCache.dylibCount);
    return &_cachedDylibsStateArray[index];
}

const MachOLoaded* RuntimeState::cachedDylibLoadAddress(uint16_t index)
{
    assert(index < this->config.dyldCache.dylibCount);
    uint64_t mTime;
    uint64_t inode;
    return (MachOLoaded*)this->config.dyldCache.addr->getIndexedImageEntry(index, mTime, inode);
}

void RuntimeState::add(const Loader* ldr)
{
    // append to list
    loaded.push_back(ldr);

    // remember special loaders
    const MachOAnalyzer* ma = ldr->analyzer(*this);
    if ( ma->isDylib() ) {
        const char* installName = ldr->analyzer(*this)->installName();
        if ( config.process.platform == dyld3::Platform::driverKit ) {
            if ( strcmp(installName, "/System/DriverKit/usr/lib/system/libdyld.dylib") == 0 )
                setDyldLoader(ldr);
            else if ( strcmp(installName, "/System/DriverKit/usr/lib/libSystem.dylib") == 0 )
                libSystemLoader = ldr;
        }
        else {
            if ( strcmp(installName, "/usr/lib/system/libdyld.dylib") == 0 )
                setDyldLoader(ldr);
            else if ( strcmp(installName, "/usr/lib/libSystem.B.dylib") == 0 )
                libSystemLoader = ldr;
        }
    }
}

void RuntimeState::setDyldLoader(const Loader* ldr)
{
    this->libdyldLoader = ldr;

    Loader::ResolvedSymbol result = { nullptr, "", 0, Loader::ResolvedSymbol::Kind::bindAbsolute, false, false };
    Diagnostics diag;
    if ( ldr->hasExportedSymbol(diag, *this, "__dyld_missing_symbol_abort", Loader::shallow, &result) )
        this->libdyldMissingSymbol = (const void*)Loader::resolvedAddress(*this, result);
}

void RuntimeState::setMainLoader(const Loader* ldr)
{
    this->mainExecutableLoader = ldr;

#if BUILDING_DYLD
    // main executable is mapped by kernel so walk mappings here to find immutable ranges and do logging
    const MachOAnalyzer* ma = ldr->analyzer(*this);
    if ( this->config.log.libraries )
        Loader::logLoad(*this, ma, this->config.process.mainExecutablePath);
    if ( this->config.log.segments ) {
        this->log("Kernel mapped %s\n", this->config.process.mainExecutablePath);
        uintptr_t        slide    = ma->getSlide();
        __block uint32_t segIndex = 0;
        ma->forEachSegment(^(const MachOAnalyzer::SegmentInfo& segInfo, bool& stop) {
            uint8_t  permissions = segInfo.protections;
            uint64_t segAddr     = segInfo.vmAddr + slide;
            uint64_t segSize     = round_page(segInfo.fileSize);
            if ( (segSize == 0) && (segIndex == 0) )
                segSize = (uint64_t)ma; // kernel stretches __PAGEZERO
            if ( this->config.log.segments ) {
                this->log("%14s (%c%c%c) 0x%012llX->0x%012llX \n", ma->segmentName(segIndex),
                          (permissions & PROT_READ) ? 'r' : '.', (permissions & PROT_WRITE) ? 'w' : '.', (permissions & PROT_EXEC) ? 'x' : '.',
                          segAddr,
                          segAddr + segSize);
            }
            segIndex++;
        });
    }
#endif

#if BUILDING_DYLD && SUPPORT_ROSETTA
    // if translated, update all_image_info
    if ( this->config.process.isTranslated ) {
        dyld_all_runtime_info* aotInfo;
        int                    ret = aot_get_runtime_info(aotInfo);
        if ( ret == 0 ) {
            for ( uint64_t i = 0; i < aotInfo->uuid_count; i++ ) {
                dyld_image_info image_info = aotInfo->images[i];
                dyld_uuid_info  uuid_info  = aotInfo->uuids[i];

                // add the arm64 Rosetta runtime to uuid info
                addNonSharedCacheImageUUID(this->longTermAllocator, uuid_info);

                // ktrace notify about main executables translation
                struct stat sb;
                if ( dyld3::stat(image_info.imageFilePath, &sb) == 0 ) {
                    fsid_t     fsid      = { { 0, 0 } };
                    fsobj_id_t fsobj     = { 0 };
                    ino_t      inode     = sb.st_ino;
                    fsobj.fid_objno      = (uint32_t)inode;
                    fsobj.fid_generation = (uint32_t)(inode >> 32);
                    fsid.val[0]          = sb.st_dev;
                    dyld3::kdebug_trace_dyld_image(DBG_DYLD_UUID_MAP_A, image_info.imageFilePath, &(uuid_info.imageUUID), fsobj, fsid, image_info.imageLoadAddress);
                }
            }

            // add aot images to dyld_all_image_info
            addAotImagesToAllAotImages(this->longTermAllocator, (uint32_t)aotInfo->aot_image_count, aotInfo->aots);

            // add the arm64 Rosetta runtime to dyld_all_image_info
            addImagesToAllImages(this->longTermAllocator, (uint32_t)aotInfo->image_count, aotInfo->images);

            // set the aot shared cache info in dyld_all_image_info
            gProcessInfo->aotSharedCacheBaseAddress = aotInfo->aot_cache_info.cacheBaseAddress;
            ::memcpy(gProcessInfo->aotSharedCacheUUID, aotInfo->aot_cache_info.cacheUUID, sizeof(uuid_t));
        }
    }
#endif // SUPPORT_ROSETTA
}

void RuntimeState::withNotifiersReadLock(void (^work)())
{
#if BUILDING_DYLD
    if ( this->libSystemHelpers != nullptr ) {
        this->libSystemHelpers->os_unfair_recursive_lock_lock_with_options(&_locks.notifiersLock, OS_UNFAIR_LOCK_NONE);
        work();
        this->libSystemHelpers->os_unfair_recursive_lock_unlock(&_locks.notifiersLock);
    }
    else
#endif
    {
        work();
    }
}

void RuntimeState::withNotifiersWriteLock(void (^work)())
{
#if BUILDING_DYLD
    if ( this->libSystemHelpers != nullptr ) {
        this->libSystemHelpers->os_unfair_recursive_lock_lock_with_options(&_locks.notifiersLock, OS_UNFAIR_LOCK_NONE);
          this->incWritable();
             work();
          this->decWritable();
        this->libSystemHelpers->os_unfair_recursive_lock_unlock(&_locks.notifiersLock);
    }
    else
#endif
    {
        work();
    }
}

void RuntimeState::withTLVLock(void (^work)())
{
#if BUILDING_DYLD
    if ( this->libSystemHelpers != nullptr ) {
        this->libSystemHelpers->os_unfair_recursive_lock_lock_with_options(&_locks.tlvInfosLock, OS_UNFAIR_LOCK_NONE);
        work();
        this->libSystemHelpers->os_unfair_recursive_lock_unlock(&_locks.tlvInfosLock);
    }
    else
#endif
    {
        work();
    }
}

void RuntimeState::addDynamicReference(const Loader* from, const Loader* to)
{
    // don't add dynamic reference if target can't be unloaded
    if ( to->neverUnload )
        return;

    withLoadersWriteLock(^{
        // don't add if already in list
        for (const DynamicReference& ref : _dynamicReferences) {
            if ( (ref.from == from) && (ref.to == to) ) {
                return;
            }
        }
        //log("addDynamicReference(%s, %s\n", from->leafName(), to->leafName());
        _dynamicReferences.push_back({from, to});
    });
}

void RuntimeState::log(const char* format, ...) const
{
    va_list list;
    va_start(list, format);
    (const_cast<RuntimeState*>(this))->vlog(format, list);
    va_end(list);
}

void RuntimeState::setUpLogging()
{
    if ( config.log.useStderr || config.log.useFile ) {
        // logging forced to a file or stderr
        _logDescriptor = config.log.descriptor;
        _logToSyslog   = false;
        _logSetUp      = true;
    }
    else {
        struct stat sb;
        if ( config.process.pid == 1 ) {
            // for launchd, write to console
            _logDescriptor = config.syscall.open("/dev/console", O_WRONLY | O_NOCTTY, 0);
            _logToSyslog   = false;
            _logSetUp      = true;
        }
        else if ( config.syscall.fstat(config.log.descriptor, &sb) >= 0 ) {
            // descriptor is open, use normal logging to it
            _logDescriptor = config.log.descriptor;
            _logToSyslog   = false;
            _logSetUp      = true;
        }
#if BUILDING_DYLD
        else {
            // Use syslog() for processes managed by launchd
            // we can only check if launchd owned after libSystem initialized
            if ( libSystemHelpers != nullptr ) {
                if ( libSystemHelpers->isLaunchdOwned() ) {
                    _logToSyslog = true;
                    _logSetUp    = true;
                }
            }
            // note: if libSystem not initialzed yet, don't set _logSetUp, but try again on next log()
        }
    #if !TARGET_OS_SIMULATOR
        if ( _logToSyslog ) {
            // if loggging to syslog, set up a socket connection
            _logDescriptor = config.syscall.socket(AF_UNIX, SOCK_DGRAM, 0);
            if ( _logDescriptor != -1 ) {
                config.syscall.fcntl(_logDescriptor, F_SETFD, (void*)1);
                struct sockaddr_un addr;
                addr.sun_family = AF_UNIX;
                strncpy(addr.sun_path, _PATH_LOG, sizeof(addr.sun_path));
                if ( config.syscall.connect(_logDescriptor, (struct sockaddr*)&addr, sizeof(addr)) == -1 ) {
                    config.syscall.close(_logDescriptor);
                    _logDescriptor = -1;
                }
            }
            if ( _logDescriptor == -1 ) {
                _logToSyslog = false;
            }
        }
    #endif // !TARGET_OS_SIMULATOR
#endif     // BUILDING_DYLD
    }
}

void RuntimeState::vlog(const char* format, va_list list)
{
#if BUILDING_CLOSURE_UTIL
    vprintf(format, list);
    return;
#else
#if BUILDING_DYLD && !TARGET_OS_SIMULATOR
    // prevent multi-thread log() calls from intermingling their text
    os_lock_lock(&_locks.logSerializer);
#endif
    // lazy initialize logging output
    if ( !_logSetUp )
        this->setUpLogging();

#if !TARGET_OS_SIMULATOR
    // write to log
    if ( _logToSyslog ) {
        // send formatted message to syslogd
        if ( _SIMPLE_STRING strbuf = ::_simple_salloc() ) {
            if ( ::_simple_sprintf(strbuf, "<%d>%s[%d]: ", LOG_USER | LOG_NOTICE, config.process.progname, config.process.pid) == 0 ) {
                if ( ::_simple_vsprintf(strbuf, format, list) == 0 ) {
                    const char* p = _simple_string(strbuf);
                    ::__sendto(_logDescriptor, p, strlen(p), 0, NULL, 0);
                }
            }
            ::_simple_sfree(strbuf);
        }
    }
    else
#endif // !TARGET_OS_SIMULATORb
    if ( _logDescriptor != -1 ) {
        // NOTE: it would be nicer to somehow merge these into one write call to reduce multithread interleaving
        ::_simple_dprintf(_logDescriptor, "dyld[%d]: ", config.process.pid);
        // write to file, stderr, or console
        ::_simple_vdprintf(_logDescriptor, format, list);
    }

#if BUILDING_DYLD && !TARGET_OS_SIMULATOR
    os_lock_unlock(&_locks.logSerializer);
#endif
#endif
}


RuntimeState::PermanentRanges* RuntimeState::PermanentRanges::make(RuntimeState& state, const Array<const Loader*>& neverUnloadLoaders)
{
    // rather that doing this in two passes, we build the ranges into a temp stack buffer, then allocate the real PermanentRanges
    STACK_ALLOC_OVERFLOW_SAFE_ARRAY(Range, tempRanges, neverUnloadLoaders.count()*8);
    for (const Loader* ldr : neverUnloadLoaders) {
        const MachOLoaded*   ma           = ldr->loadAddress(state);
        const uintptr_t      slide        = ma->getSlide();
        __block uintptr_t    lastSegEnd   = 0;
        __block uint8_t      lastPerms    = 0;
        ldr->loadAddress(state)->forEachSegment(^(const MachOAnalyzer::SegmentInfo& segInfo, bool& stop) {
            uintptr_t segStart = (uintptr_t)(segInfo.vmAddr + slide);
            uintptr_t segEnd   = segStart + (uintptr_t)segInfo.vmSize;
            if ( (segStart == lastSegEnd) && (segInfo.protections == lastPerms) && !tempRanges.empty() ) {
                // back to back segments with same perms, so just extend last range
                tempRanges.back().end = segEnd;
            }
            else if ( segInfo.protections != 0 ) {
                Range r;
                r.start       = segStart;
                r.end         = segEnd;
                r.permissions = segInfo.protections;
                r.loader      = ldr;
                tempRanges.push_back(r);
            }
            lastSegEnd = segEnd;
            lastPerms  = segInfo.protections;
        });
    }
    unsigned count = (unsigned)tempRanges.count();
    PermanentRanges* p = (PermanentRanges*)state.longTermAllocator.malloc(offsetof(PermanentRanges, _ranges[count]));
    p->_next.store(nullptr);
    p->_rangeCount = count;
    for (unsigned i=0; i < count; ++i)
        p->_ranges[i] = tempRanges[i];
    return p;
}

bool RuntimeState::PermanentRanges::contains(uintptr_t start, uintptr_t end, uint8_t* perms, const Loader** loader) const
{
    for (uint i=0; i < _rangeCount; ++i) {
        const Range& range = _ranges[i];
        if ( (range.start <= start) && (range.end > end) ) {
            *perms  = range.permissions;
            *loader = range.loader;
            return true;
        }
    }
    return false;
}

RuntimeState::PermanentRanges* RuntimeState::PermanentRanges::next() const
{
    return this->_next.load(std::memory_order_acquire);
}

void RuntimeState::PermanentRanges::append(PermanentRanges* pr)
{
    // if _next is unused, set it to 'pr', otherwise recurse down linked list
    PermanentRanges* n = _next.load(std::memory_order_acquire);
    if ( n == nullptr )
        _next.store(pr, std::memory_order_release);
    else
        n->append(pr);
}

void RuntimeState::addPermanentRanges(const Array<const Loader*>& neverUnloadLoaders)
{
    PermanentRanges* pr = PermanentRanges::make(*this, neverUnloadLoaders);
    if ( _permanentRanges == nullptr )
        _permanentRanges = pr;
    else
        _permanentRanges->append(pr);
}

bool RuntimeState::inPermanentRange(uintptr_t start, uintptr_t end, uint8_t* perms, const Loader** loader)
{
    for (const PermanentRanges* p = _permanentRanges; p != nullptr; p = p->next()) {
        if ( p->contains(start, end, perms, loader) )
            return true;
    }
    return false;
}


// if a dylib interposes a function which would be in the dyld cache, except there is a dylib
// overriding the cache, we need to record the original address of the function in the cache
// in order to patch other parts of the cache (to use the interposer function)
void RuntimeState::checkHiddenCacheAddr(const Loader* targetLoader, const void* targetAddr, const char* symbolName,
                                        dyld3::OverflowSafeArray<HiddenCacheAddr>& hiddenCacheAddrs) const
{
   if ( targetLoader != nullptr ) {
        if ( const JustInTimeLoader* jl = targetLoader->isJustInTimeLoader() ) {
            const Loader::DylibPatch*   patchTable;
            uint16_t                    cacheDylibOverriddenIndex;
            if ( jl->overridesDylibInCache(patchTable, cacheDylibOverriddenIndex) ) {
                uint64_t mTime;
                uint64_t inode;
                if ( const MachOAnalyzer* overriddenMA = (MachOAnalyzer*)config.dyldCache.addr->getIndexedImageEntry(cacheDylibOverriddenIndex, mTime, inode) ) {
                    void* functionAddrInCache;
                    bool  resultPointsToInstructions;
                    if ( overriddenMA->hasExportedSymbol(symbolName, nullptr, &functionAddrInCache, &resultPointsToInstructions) ) {
                        hiddenCacheAddrs.push_back({functionAddrInCache, targetAddr});
                    }
                }
            }
        }
    }
}


void RuntimeState::appendInterposingTuples(const Loader* ldr, const uint8_t* rawDylibTuples, uint32_t tupleCount)
{
    // AMFI can ban interposing
    if ( !config.security.allowInterposing )
        return;

    // make a temp array of tuples for use while binding
    struct TuplePlus { InterposeTupleSpecific tuple; const char* symbolName; };
    STACK_ALLOC_ARRAY(TuplePlus, tempTuples, tupleCount);
    const TuplePlus empty = { {nullptr, 0, 0}, nullptr };
    for ( uint32_t i = 0; i < tupleCount; ++i )
        tempTuples.push_back(empty);
    const uintptr_t* rawStart = (uintptr_t*)rawDylibTuples;
    const uintptr_t* rawEnd   = &rawStart[2 * tupleCount];

    // if cached dylib is overridden and interposed keep track of cache address for later patching
    STACK_ALLOC_OVERFLOW_SAFE_ARRAY(HiddenCacheAddr, hiddenCacheAddrs, 32);

    // The __interpose section has a bind and rebase for each entry.  We have to eval those to make a tuple.
    // This has to be done before the real fixups are applied because the real fixups need the tuples to be already built.
    __block Diagnostics  diag;
    const MachOAnalyzer* ma    = ldr->analyzer(*this);
    if ( ma->hasChainedFixups() ) {
        ma->withChainStarts(diag, 0, ^(const dyld_chained_starts_in_image* starts) {
            STACK_ALLOC_OVERFLOW_SAFE_ARRAY(const void*, targetAddrs, 128);
            STACK_ALLOC_OVERFLOW_SAFE_ARRAY(const char*, targetNames, 128);
            ma->forEachChainedFixupTarget(diag, ^(int libOrdinal, const char* symbolName, uint64_t addend, bool weakImport, bool& stop) {
                Loader::ResolvedSymbol target = ldr->resolveSymbol(diag, *this, libOrdinal, symbolName, weakImport, false, nullptr);
                targetAddrs.push_back((void*)(Loader::resolvedAddress(*this, target) + addend));
                checkHiddenCacheAddr(target.targetLoader, targetAddrs.back(), symbolName, hiddenCacheAddrs);
                targetNames.push_back(symbolName);
            });
            const uintptr_t prefLoadAddresss = (uintptr_t)(ma->preferredLoadAddress());
            ma->forEachFixupInAllChains(diag, starts, false, ^(MachOLoaded::ChainedFixupPointerOnDisk* fixupLoc, const dyld_chained_starts_in_segment* segInfo, bool& stop) {
                if ( ((uintptr_t*)fixupLoc >= rawStart) && ((uintptr_t*)fixupLoc < rawEnd) ) {
                    uintptr_t index      = ((uintptr_t*)fixupLoc - rawStart) / 2;
                    if ( index * 2 == (uintptr_t)(((uintptr_t*)fixupLoc - rawStart)) ) {
                        uint64_t targetRuntimeOffset;
                        if ( fixupLoc->isRebase(segInfo->pointer_format, prefLoadAddresss, targetRuntimeOffset) ) {
                            tempTuples[index].tuple.replacement = (uintptr_t)ma + (uintptr_t)targetRuntimeOffset;
                            tempTuples[index].tuple.onlyImage   = ldr;
                            //this->log("replacement=0x%08lX at %lu in %s\n", interposingTuples[index].replacement, index, ldr->path());
                        }
                    }
                    else {
                        uint32_t bindOrdinal;
                        int64_t  addend;
                        if ( fixupLoc->isBind(segInfo->pointer_format, bindOrdinal, addend) ) {
                            tempTuples[index].tuple.replacee = (uintptr_t)targetAddrs[bindOrdinal];
                            tempTuples[index].symbolName     = targetNames[bindOrdinal];
                            //this->log("replacee=0x%08lX at %lu for %s in %s\n", tempTuples[index].tuple.replacee, index, tempTuples[index].symbolName, ldr->path());
                        }
                    }
                }
            });
        });
    }
    else {
        // rebase
        intptr_t slide = (uintptr_t)ma - (uintptr_t)ma->preferredLoadAddress();
        ma->forEachRebase(diag, false, ^(uint64_t runtimeOffset, bool& stop) {
            uintptr_t* fixupLoc = (uintptr_t*)((uint64_t)ma + runtimeOffset);
            if ( (fixupLoc >= rawStart) && (fixupLoc < rawEnd) ) {
                // the first column (replacement) in raw tuples are rebases
                uintptr_t index                           = (fixupLoc - rawStart) / 2;
                uintptr_t replacement                     = *fixupLoc + slide;
                tempTuples[index].tuple.replacement = replacement;
                tempTuples[index].tuple.onlyImage   = ldr;
                //this->log("replacement=0x%08lX at %lu in %s\n", replacement, index, ldr->path());
            }
        });

        // bind
        ma->forEachBind(diag, ^(uint64_t runtimeOffset, int libOrdinal, uint8_t type, const char* symbolName, bool weakImport, bool lazyBind, uint64_t addend, bool& stop) {
            uintptr_t* fixupLoc = (uintptr_t*)((uint64_t)ma + runtimeOffset);
            if ( (fixupLoc >= rawStart) && (fixupLoc < rawEnd) ) {
                Loader::ResolvedSymbol target = ldr->resolveSymbol(diag, *this, libOrdinal, symbolName, weakImport, lazyBind, nullptr);
                if ( diag.noError() ) {
                    uintptr_t index            = (fixupLoc - rawStart) / 2;
                    uintptr_t replacee         = Loader::resolvedAddress(*this, target) + (uintptr_t)addend;
                    tempTuples[index].tuple.replacee = replacee;
                    tempTuples[index].symbolName     = symbolName;
                    checkHiddenCacheAddr(target.targetLoader, (const void*)replacee, symbolName, hiddenCacheAddrs);
                    //this->log("replacee=0x%08lX at %lu in %s\n", replacee, index, ldr->path());
                 }
            } 
        },
        ^(const char*) {});
    }

    // transfer temp tuples to interposingTuples
    for ( TuplePlus& t : tempTuples ) {
        // ignore tuples where one of the pointers is NULL
        if ( (t.tuple.replacee == 0) || (t.tuple.replacement == 0) )
            continue;

        // add generic interpose for all images, if one already exists, alter it
        uintptr_t previousReplacement = 0;
        for ( InterposeTupleAll& existing : this->interposingTuplesAll ) {
            if ( existing.replacee == t.tuple.replacee ) {
                previousReplacement = existing.replacement;
                existing.replacement = t.tuple.replacement;
            }
        }
        if ( previousReplacement == 0 )
            this->interposingTuplesAll.push_back({t.tuple.replacement, t.tuple.replacee});
        if ( this->config.log.interposing )
            this->log("%s has interposed '%s' to replacing binds to 0x%08lX with 0x%08lX\n", ldr->leafName(), t.symbolName, t.tuple.replacee, t.tuple.replacement);

        // now add specific interpose so that the generic is not applied to the interposing dylib, so it can call through to old impl
        if ( previousReplacement != 0 ) {
            // need to chain to previous interpose replacement
            this->interposingTuplesSpecific.push_back({ldr, previousReplacement, t.tuple.replacee});
            if ( this->config.log.interposing )
                this->log("   '%s' was previously interposed, so chaining 0x%08lX to call through to 0x%08lX\n", t.symbolName, t.tuple.replacement, previousReplacement);
        }
        else {
            this->interposingTuplesSpecific.push_back({ldr, t.tuple.replacee, t.tuple.replacee});
        }

        // if the replacee is in a dylib that overrode the dyld cache, we need to
        // add a tuple to replace the original cache impl address for cache patching to work
        for ( const HiddenCacheAddr& entry : hiddenCacheAddrs ) {
            if ( entry.replacementAddr == (void*)(t.tuple.replacee) ) {
                this->interposingTuplesAll.push_back({t.tuple.replacement, (uintptr_t)entry.cacheAddr});
                if ( this->config.log.interposing )
                    this->log("%s has interposed '%s' so need to patch cache uses of 0x%08lX\n", ldr->leafName(), t.symbolName, (uintptr_t)entry.cacheAddr);
            }
        }
    }
}

void RuntimeState::buildInterposingTables()
{
    // AMFI can ban interposing
    if ( !config.security.allowInterposing )
        return;

    // look for __interpose section in dylibs loaded at launch
    const uint32_t   pointerSize = sizeof(void*);
    __block uint32_t tupleCount  = 0;
    STACK_ALLOC_OVERFLOW_SAFE_ARRAY(const Loader*, dylibsWithTuples, 8);
    for ( const Loader* ldr : loaded ) {
        const MachOAnalyzer* ma = ldr->analyzer(*this);
        if ( !ma->isDylib() )
            continue;
        if ( ldr->dylibInDyldCache )
            continue;
        Diagnostics diag;
        ma->forEachInterposingSection(diag, ^(uint64_t vmOffset, uint64_t vmSize, bool& stop) {
            tupleCount += (vmSize / (2 * pointerSize));
            dylibsWithTuples.push_back(ldr);
        });
    }
    if ( tupleCount == 0 )
        return;

    // fixups have not been apply yet.  We need to peek ahead to resolve the __interpose section content
    interposingTuplesAll.reserve(tupleCount);
    interposingTuplesSpecific.reserve(tupleCount);
    for ( const Loader* ldr : dylibsWithTuples ) {
        Diagnostics          diag;
        const MachOAnalyzer* ma = ldr->analyzer(*this);
        ma->forEachInterposingSection(diag, ^(uint64_t vmOffset, uint64_t vmSize, bool& stop) {
            this->appendInterposingTuples(ldr, (uint8_t*)ma + vmOffset, (uint32_t)(vmSize / (2 * pointerSize)));
        });
    }
}

void RuntimeState::setLaunchMissingDylib(const char* missingDylibPath, const char* clientUsingDylib)
{
    gProcessInfo->errorKind              = DYLD_EXIT_REASON_DYLIB_MISSING;
    gProcessInfo->errorClientOfDylibPath = clientUsingDylib;
    gProcessInfo->errorTargetDylibPath   = missingDylibPath;
    gProcessInfo->errorSymbol            = nullptr;
}

void RuntimeState::setLaunchMissingSymbol(const char* missingSymbolName, const char* dylibThatShouldHaveSymbol, const char* clientUsingSymbol)
{
    gProcessInfo->errorKind              = DYLD_EXIT_REASON_SYMBOL_MISSING;
    gProcessInfo->errorClientOfDylibPath = clientUsingSymbol;
    gProcessInfo->errorTargetDylibPath   = dylibThatShouldHaveSymbol;
    gProcessInfo->errorSymbol            = missingSymbolName;
}

void RuntimeState::addMissingFlatLazySymbol(const Loader* ldr, const char* symbolName, uintptr_t* bindLoc)
{
    _missingFlatLazySymbols.push_back({ ldr, symbolName, bindLoc });
}

void RuntimeState::rebindMissingFlatLazySymbols(const dyld3::Array<const Loader*>& newLoaders)
{
    // FIXME: Do we want to drop diagnostics here?  We don't want to fail a dlopen because a missing
    // symbol lookup caused an error
    Diagnostics diag;

    _missingFlatLazySymbols.erase(std::remove_if(_missingFlatLazySymbols.begin(), _missingFlatLazySymbols.end(), [&](const MissingFlatSymbol& symbol) {
        Loader::ResolvedSymbol result = { nullptr, symbol.symbolName, 0, Loader::ResolvedSymbol::Kind::bindAbsolute, false, false };
        for ( const Loader* ldr : newLoaders ) {
            // flat lookup can look in self, even if hidden
            if ( ldr->hiddenFromFlat() )
                continue;
            if ( ldr->hasExportedSymbol(diag, *this, symbol.symbolName, Loader::shallow, &result) ) {
                // Note we don't try to interpose here.  Interposing is only registered at launch, when we know the symbol wasn't defined
                uintptr_t targetAddr = Loader::resolvedAddress(*this, result);
                if ( this->config.log.fixups )
                    this->log("fixup: *0x%012lX = 0x%012lX <%s>\n", (uintptr_t)symbol.bindLoc, (uintptr_t)targetAddr, ldr->leafName());
                *symbol.bindLoc = targetAddr;
                this->addDynamicReference(symbol.ldr, result.targetLoader);
                return true;
            }
        }
        return false;
    }), _missingFlatLazySymbols.end());
}

void RuntimeState::removeMissingFlatLazySymbols(const dyld3::Array<const Loader*>& removingLoaders)
{
    _missingFlatLazySymbols.erase(std::remove_if(_missingFlatLazySymbols.begin(), _missingFlatLazySymbols.end(), [&](const MissingFlatSymbol& symbol) {
        return removingLoaders.contains(symbol.ldr);
    }), _missingFlatLazySymbols.end());
}

bool RuntimeState::hasMissingFlatLazySymbols() const
{
    return !_missingFlatLazySymbols.empty();
}

// <rdar://problem/29099600> dyld should tell the kernel when it is doing root fix-ups
void RuntimeState::setVMAccountingSuspending(bool suspend)
{
#if TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR
    if ( suspend == _vmAccountingSuspended )
        return;
    if ( this->config.log.fixups )
        this->log("set vm.footprint_suspend=%d\n", suspend);
    int    newValue = suspend ? 1 : 0;
    int    oldValue = 0;
    size_t newlen   = sizeof(newValue);
    size_t oldlen   = sizeof(oldValue);
    int    ret      = ::sysctlbyname("vm.footprint_suspend", &oldValue, &oldlen, &newValue, newlen);
    if ( this->config.log.fixups && (ret != 0) )
        this->log("vm.footprint_suspend => %d, errno=%d\n", ret, errno);
    _vmAccountingSuspended = suspend;
#endif
}

void RuntimeState::incDlRefCount(const Loader* ldr)
{
    // don't track dlopen ref-counts for things that never unload
    if ( ldr->neverUnload )
        return;

    // check for existing entry
    for ( DlopenCount& entry : _dlopenRefCounts ) {
        if ( entry.loader == ldr ) {
            // found existing DlopenCount entry, bump counter
            entry.refCount += 1;
            return;
        }
    }

    // no existing DlopenCount, add new one
    _dlopenRefCounts.push_back({ ldr, 1 });
}

void RuntimeState::decDlRefCount(const Loader* ldr)
{
    // don't track dlopen ref-counts for things that never unload
    if ( ldr->neverUnload )
        return;

    this->incWritable();

    bool doCollect = false;
    for (auto it=_dlopenRefCounts.begin(); it != _dlopenRefCounts.end(); ++it) {
        if ( it->loader == ldr ) {
            // found existing DlopenCount entry, bump counter
            it->refCount -= 1;
            if ( it->refCount == 0 ) {
                _dlopenRefCounts.erase(it);
                doCollect = true;
                break;
            }
            return;
        }
    }
    if ( doCollect )
        garbageCollectImages();

    this->decWritable();
}

class VIS_HIDDEN Reaper
{
public:
    struct LoaderAndUse
    {
        const Loader* loader;
        bool          inUse;
    };
    Reaper(RuntimeState& state, Array<LoaderAndUse>& unloadables);
    void garbageCollect();
    void finalizeDeadImages();
    void runTerminators(const Loader* ldr);

private:
    void     markDirectlyDlopenedImagesAsUsed();
    void     markDynamicNeverUnloadImagesAsUsed();
    void     markDependentOfInUseImages();
    void     markDependentsOf(const Loader* ldr);
    uint32_t inUseCount();
    void     dump(const char* msg);

    RuntimeState&        _state;
    Array<LoaderAndUse>& _unloadables;
    uint32_t             _deadCount;
};

Reaper::Reaper(RuntimeState& state, Array<LoaderAndUse>& unloadables)
    : _state(state)
    , _unloadables(unloadables)
    , _deadCount(0)
{
}

void Reaper::markDirectlyDlopenedImagesAsUsed()
{
    for ( const RuntimeState::DlopenCount& entry : _state._dlopenRefCounts ) {
        if ( entry.refCount != 0 ) {
            for ( LoaderAndUse& lu : _unloadables ) {
                if ( lu.loader == entry.loader ) {
                    lu.inUse = true;
                    break;
                }
            }
        }
    }
}

void Reaper::markDynamicNeverUnloadImagesAsUsed()
{
    for ( const Loader* ldr : _state._dynamicNeverUnloads ) {
        for ( LoaderAndUse& lu : _unloadables ) {
            if ( lu.loader == ldr ) {
                lu.inUse = true;
                break;
            }
        }
    }
}

uint32_t Reaper::inUseCount()
{
    uint32_t count = 0;
    for ( LoaderAndUse& iu : _unloadables ) {
        if ( iu.inUse )
            ++count;
    }
    return count;
}

void Reaper::markDependentsOf(const Loader* ldr)
{
    // mark static dependents
    const uint32_t depCount = ldr->dependentCount();
    for ( uint32_t depIndex = 0; depIndex < depCount; ++depIndex ) {
        const Loader* child = ldr->dependent(_state, depIndex);
        for ( LoaderAndUse& lu : _unloadables ) {
            if ( !lu.inUse && (lu.loader == child) ) {
                lu.inUse = true;
                break;
            }
        }
    }

    // mark dynamic dependents
    for ( const RuntimeState::DynamicReference& ref : _state._dynamicReferences ) {
        if ( ref.from == ldr ) {
            for ( LoaderAndUse& lu : _unloadables ) {
                if ( lu.loader == ref.to ) {
                    //_state.log("markDependentsOf(%s) dynamic ref to %s\n", ldr->leafName(), ref.to->leafName());
                    lu.inUse = true;
                    break;
                }
            }
        }
    }
}

void Reaper::markDependentOfInUseImages()
{
    for ( LoaderAndUse& lu : _unloadables ) {
        if ( lu.inUse )
            markDependentsOf(lu.loader);
    }
}

void Reaper::dump(const char* msg)
{
    _state.log("GC, %s:\n", msg);
    for (LoaderAndUse& lu : _unloadables) {
        _state.log("  in-use=%d  %s\n", lu.inUse, lu.loader->path());
    }
}

void Reaper::garbageCollect()
{
    const bool verbose = false;

    if (verbose) dump("all unloadable images");

    // mark all dylibs directly dlopen'ed as in use
    markDirectlyDlopenedImagesAsUsed();

    // Mark dylibs with dynamic never unloads as in use
    markDynamicNeverUnloadImagesAsUsed();

    if (verbose) dump("directly dlopen()'ed marked");

    // iteratively mark dependents of in-use dylibs as in-use until in-use count stops changing
    uint32_t lastCount    = inUseCount();
    bool     countChanged = false;
    do {
        markDependentOfInUseImages();
        if (verbose) dump("dependents marked");
        uint32_t newCount = inUseCount();
        countChanged      = (newCount != lastCount);
        lastCount         = newCount;
    } while ( countChanged );

    _deadCount = (uint32_t)_unloadables.count() - inUseCount();
}

void Reaper::finalizeDeadImages()
{
    if ( _deadCount == 0 )
        return;

#if BUILDING_DYLD
    if ( _state.libSystemHelpers != nullptr ) {
        STACK_ALLOC_OVERFLOW_SAFE_ARRAY(__cxa_range_t, ranges, _deadCount);
        for ( LoaderAndUse& lu : _unloadables ) {
            if ( lu.inUse )
                continue;
            const MachOAnalyzer* ma = lu.loader->analyzer(_state);
            if ( lu.loader->dylibInDyldCache )
                continue;
            runTerminators(lu.loader);
            ma->forEachSegment(^(const MachOAnalyzer::SegmentInfo& segInfo, bool& stop) {
                if ( segInfo.executable() ) {
                    __cxa_range_t range;
                    range.addr   = (void*)(segInfo.vmAddr + ma->getSlide());
                    range.length = (size_t)segInfo.vmSize;
                    ranges.push_back(range);
                }
            });
        }
        _state.libSystemHelpers->__cxa_finalize_ranges(ranges.begin(), (uint32_t)ranges.count());
    }
#endif
}

void Reaper::runTerminators(const Loader* ldr)
{
    const MachOAnalyzer*           ma = ldr->analyzer(_state);
    Diagnostics                    diag;
    MachOAnalyzer::VMAddrConverter vmAddrConverter = ma->makeVMAddrConverter(true);
    // <rdar://problem/71820555> Don't run static terminator for arm64e
    if ( ma->isArch("arm64e") )
        return;
    if ( ma->hasTerminators(diag, vmAddrConverter) ) {
        typedef void (*Terminator)();
        ma->forEachTerminator(diag, vmAddrConverter, ^(uint32_t offset) {
            Terminator termFunc = (Terminator)((uint8_t*)ma + offset);
            termFunc();
            if ( _state.config.log.initializers )
                _state.log("called static terminator %p in %s\n", termFunc, ldr->path());
        });
    }
}

// This function is called at the end of dlclose() when the reference count goes to zero.
// The dylib being unloaded may have brought in other dependent dylibs when it was loaded.
// Those dependent dylibs need to be unloaded, but only if they are not referenced by
// something else.  We use a standard mark and sweep garbage collection.
//
// The tricky part is that when a dylib is unloaded it may have a termination function that
// can run and itself call dlclose() on yet another dylib.  The problem is that this
// sort of gabage collection is not re-entrant.  Instead a terminator's call to dlclose()
// which calls garbageCollectImages() will just set a flag to re-do the garbage collection
// when the current pass is done.
//
// Also note that this is done within the _apiLock lock, so any dlopen/dlclose
// on other threads are blocked while this garbage collections runs.
//
void RuntimeState::garbageCollectImages()
{
    // if GC is already being done, just bump count, so GC does an extra interation
    int32_t prevCount = atomic_fetch_add_explicit(&_gcCount, 1, std::memory_order_relaxed);
    if ( prevCount != 0 )
        return;

    // if some termination routine called GC during our work, redo GC on its behalf
    do {
        garbageCollectInner();
        prevCount = atomic_fetch_add_explicit(&_gcCount, -1, std::memory_order_relaxed);
    } while ( prevCount > 1 );
}

void RuntimeState::garbageCollectInner()
{
    static const bool verbose = false;

    STACK_ALLOC_ARRAY(Reaper::LoaderAndUse, unloadables, loaded.size());
    withLoadersReadLock(^{
        for ( const Loader* ldr : loaded ) {
            if ( !ldr->dylibInDyldCache ) {
                bool inUse = ldr->neverUnload;
                unloadables.push_back({ ldr, inUse });
                if ( verbose )
                    this->log("unloadable[%lu] neverUnload=%d %p %s\n", unloadables.count(), inUse, ldr->loadAddress(*this), ldr->path());
            }
        }
    });
    // make reaper object to do garbage collection and notifications
    Reaper reaper(*this, unloadables);
    reaper.garbageCollect();

    // FIXME: we should sort dead images so higher level ones are terminated first

    // call cxa_finalize_ranges and static terminators of dead images
    reaper.finalizeDeadImages();

    if ( verbose ) {
        this->log("loaded before GC removals:\n");
        for ( const Loader* ldr : loaded )
            this->log("   loadAddr=%p, path=%s\n", ldr->loadAddress(*this), ldr->path());
    }

    // make copy of LoadedImages we want to remove
    // because unloadables[] points into LoadedImage we are shrinking
    STACK_ALLOC_ARRAY(const Loader*, loadersToRemove, unloadables.count());
    for ( const Reaper::LoaderAndUse& lu : unloadables ) {
        if ( !lu.inUse )
            loadersToRemove.push_back(lu.loader);
    }
    // remove entries from loaded
    if ( !loadersToRemove.empty() ) {
        notifyUnload(loadersToRemove);
        removeLoaders(loadersToRemove);
    }

    if ( verbose ) {
        this->log("loaded after GC removals:\n");
        for ( const Loader* ldr : loaded )
            this->log("   loadAddr=%p, path=%s\n", ldr->loadAddress(*this), ldr->path());
    }
}

void RuntimeState::notifyDebuggerLoad(const Loader* oneLoader)
{
    STACK_ALLOC_ARRAY(const Loader*, arrayOfOne, 1);
    arrayOfOne.push_back(oneLoader);
    this->notifyDebuggerLoad(arrayOfOne);
}

void RuntimeState::notifyDebuggerLoad(const dyld3::Array<const Loader*>& newLoaders)
{
    // early out if nothing to do
    if ( newLoaders.empty() )
        return;

    // notify debugger
    STACK_ALLOC_ARRAY(dyld_image_info, oldDyldInfo, newLoaders.count());
    for ( const Loader* ldr : newLoaders ) {
        FileID ldrFileID = ldr->fileID();
        uint64_t mtime = ldrFileID.valid() ? ldrFileID.mtime() : 0;
        oldDyldInfo.push_back({ ldr->loadAddress(*this), ldr->path(), (uintptr_t)mtime });
        // for images not in dyld cache, add to uuid array
        if ( !ldr->dylibInDyldCache ) {
            dyld_uuid_info dyldUuidInfo;
            dyldUuidInfo.imageLoadAddress = ldr->loadAddress(*this);
            ((MachOFile*)dyldUuidInfo.imageLoadAddress)->getUuid(dyldUuidInfo.imageUUID);
            addNonSharedCacheImageUUID(this->longTermAllocator, dyldUuidInfo);
        }
    }
    addImagesToAllImages(this->longTermAllocator, (uint32_t)oldDyldInfo.count(), &oldDyldInfo[0]);
    gProcessInfo->notification(dyld_image_adding, (uint32_t)oldDyldInfo.count(), &oldDyldInfo[0]);
}

void RuntimeState::notifyDebuggerUnload(const dyld3::Array<const Loader*>& removingLoaders)
{
    // notify debugger
    STACK_ALLOC_ARRAY(dyld_image_info, oldDyldInfo, removingLoaders.count());
    for ( const Loader* ldr : removingLoaders ) {
        oldDyldInfo.push_back({ ldr->loadAddress(*this), ldr->path(), 0 });
        removeImageFromAllImages(ldr->loadAddress(*this));
    }
    gProcessInfo->notification(dyld_image_removing, (uint32_t)oldDyldInfo.count(), &oldDyldInfo[0]);
}

// dylibs can have DOF sections which contain info about "static user probes" for dtrace
// this method finds and registers any such sections
void RuntimeState::notifyDtrace(const dyld3::Array<const Loader*>& newLoaders)
{
    static const bool verbose = false;

    // do nothing when dtrace disabled
    if ( !config.syscall.dtraceUserProbesEnabled() ) {
        if (verbose) log("dtrace probes disabled\n");
        return;
    }

    // stack allocate maximum size buffer
    uint8_t buffer[sizeof(dof_helper_t)*(newLoaders.count()+16)];
    dof_ioctl_data_t* dofData = (dof_ioctl_data_t*)buffer;
    dofData->dofiod_count = 0;

    // find dtrace DOF sections and append each to array
    __block bool someUnloadable = false;
    for (const Loader* ldr : newLoaders) {
        Diagnostics          diag;
        const MachOAnalyzer* ma = ldr->analyzer(*this);
        ma->forEachDOFSection(diag, ^(uint32_t offset) {
            dof_helper_t& entry = dofData->dofiod_helpers[dofData->dofiod_count];
            entry.dofhp_addr = (uintptr_t)ma + offset;
            entry.dofhp_dof  = (uintptr_t)ma + offset;
            strlcpy(entry.dofhp_mod, ldr->leafName(), DTRACE_MODNAMELEN);
            if (verbose) log("adding DOF section at offset 0x%08X from %s\n", offset, ldr->path());
            dofData->dofiod_count++;
            if ( !ldr->neverUnload )
                someUnloadable = true;
        });
    }

    // skip ioctl() if no DOF sections
    if ( dofData->dofiod_count == 0 )
        return;

    // register DOF sections with the kernel
    config.syscall.dtraceRegisterUserProbes(dofData);

    // record the registration ID of unloadable code so the probes can be unregistered later
    if ( someUnloadable )  {
        for (const Loader* ldr : newLoaders) {
            // don't bother to record registrationID of images that will never be unloaded
            if ( ldr->neverUnload )
                continue;
            const MachOAnalyzer* ma = ldr->analyzer(*this);
            for (uint64_t i=0; i < dofData->dofiod_count; ++i) {
                dof_helper_t& entry = dofData->dofiod_helpers[i];
                if ( entry.dofhp_addr == (uintptr_t)ma ) {
                    // the ioctl() returns the dofhp_dof field as a registrationID
                    int registrationID = (int)entry.dofhp_dof;
                    if (verbose) log("adding registrationID=%d for %s\n", registrationID, ldr->path());
                    _loadersNeedingDOFUnregistration.push_back({ldr, registrationID});
                }
            }
        }
    }
}

void RuntimeState::notifyLoad(const dyld3::Array<const Loader*>& newLoaders)
{
    const uint32_t count = (uint32_t)newLoaders.count();
// call kdebug trace for each image
#if !TARGET_OS_SIMULATOR
    if ( kdebug_is_enabled(KDBG_CODE(DBG_DYLD, DBG_DYLD_UUID, DBG_DYLD_UUID_MAP_A)) ) {
        for ( const Loader* ldr : newLoaders ) {
            const MachOLoaded* ml = ldr->loadAddress(*this);
            struct stat        stat_buf;
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

    // call each _dyld_register_func_for_add_image function with each image
    withNotifiersReadLock(^{
        for ( NotifyFunc func : _notifyAddImage ) {
            for ( const Loader* ldr : newLoaders ) {
                const MachOLoaded* ml = ldr->loadAddress(*this);
                dyld3::ScopedTimer timer(DBG_DYLD_TIMING_FUNC_FOR_ADD_IMAGE, (uint64_t)ml, (uint64_t)func, 0);
                if ( this->config.log.notifications )
                    this->log("notifier %p called with mh=%p\n", func, ml);
                if ( ldr->dylibInDyldCache )
                    func(ml, this->config.dyldCache.slide);
                else
                    func(ml, ml->getSlide());
            }
        }
        for ( LoadNotifyFunc func : _notifyLoadImage ) {
            for ( const Loader* ldr : newLoaders ) {
                const MachOLoaded* ml = ldr->loadAddress(*this);
                dyld3::ScopedTimer timer(DBG_DYLD_TIMING_FUNC_FOR_ADD_IMAGE, (uint64_t)ml, (uint64_t)func, 0);
                if ( this->config.log.notifications )
                    this->log("notifier %p called with mh=%p\n", func, ml);
                func(ml, ldr->path(), !ldr->neverUnload);
            }
        }
        for ( BulkLoadNotifier func : _notifyBulkLoadImage ) {
            const mach_header* mhs[count];
            const char*        paths[count];
            for ( unsigned i = 0; i < count; ++i ) {
                mhs[i]   = newLoaders[i]->loadAddress(*this);
                paths[i] = newLoaders[i]->path();
            }
            dyld3::ScopedTimer timer(DBG_DYLD_TIMING_FUNC_FOR_ADD_IMAGE, (uint64_t)mhs[0], (uint64_t)func, 0);
            if ( this->config.log.notifications )
                this->log("bulk notifier %p called with %d images\n", func, count);
            func(count, mhs, paths);
        }
    });

    // notify objc about images that use objc
    uint32_t           loadersWithObjC = 0;
    const char*        pathsBuffer[count];
    const mach_header* mhBuffer[count];
    if ( _notifyObjCMapped != nullptr ) {
        for ( const Loader* ldr : newLoaders ) {
            if ( ldr->hasObjC ) {
                pathsBuffer[loadersWithObjC] = ldr->path();
                mhBuffer[loadersWithObjC]    = ldr->loadAddress(*this);
                ++loadersWithObjC;
            }
        }
        if ( loadersWithObjC != 0 ) {
            dyld3::ScopedTimer timer(DBG_DYLD_TIMING_OBJC_MAP, 0, 0, 0);
            _notifyObjCMapped(loadersWithObjC, pathsBuffer, mhBuffer);
            if ( this->config.log.notifications ) {
                this->log("objc-mapped-notifier called with %d images:\n", loadersWithObjC);
                for ( uint32_t i = 0; i < loadersWithObjC; ++i ) {
                    this->log(" objc-mapped: %p %s\n", mhBuffer[i], pathsBuffer[i]);
                }
            }
        }
    }
#if BUILDING_DYLD
    // notify any other processing inspecting this one
    for ( uint32_t i = 0; i < count; ++i ) {
        pathsBuffer[i] = newLoaders[i]->path();
        mhBuffer[i]    = newLoaders[i]->loadAddress(*this);
    }
    notifyMonitoringDyld(false, count, mhBuffer, pathsBuffer);
#endif // BUILDING_DYLD
}

void RuntimeState::notifyUnload(const dyld3::Array<const Loader*>& loadersToRemove)
{
    // call each _dyld_register_func_for_remove_image function with each image
    withNotifiersReadLock(^{
        for ( NotifyFunc func : _notifyRemoveImage ) {
            for ( const Loader* ldr : loadersToRemove ) {
                const MachOLoaded* ml = ldr->loadAddress(*this);
                dyld3::ScopedTimer timer(DBG_DYLD_TIMING_FUNC_FOR_REMOVE_IMAGE, (uint64_t)ml, (uint64_t)func, 0);
                if ( this->config.log.notifications )
                    this->log("remove notifier %p called with mh=%p\n", func, ml);
                if ( ldr->dylibInDyldCache )
                    func(ml, this->config.dyldCache.slide);
                else
                    func(ml, ml->getSlide());
            }
        }
    });

    // call objc about images going away
    if ( _notifyObjCUnmapped != nullptr ) {
        for ( const Loader* ldr : loadersToRemove ) {
            if ( ldr->hasObjC ) {
                (*_notifyObjCUnmapped)(ldr->path(), ldr->loadAddress(*this));
                if ( this->config.log.notifications )
                    this->log("objc-unmapped-notifier called with image %p %s\n", ldr->loadAddress(*this), ldr->path());
            }
        }
    }

    // call kdebug trace for each image
    if ( kdebug_is_enabled(KDBG_CODE(DBG_DYLD, DBG_DYLD_UUID, DBG_DYLD_UUID_MAP_A)) ) {
        for ( const Loader* ldr : loadersToRemove ) {
            uuid_t      uuid;
            fsid_t      fsid    = { { 0, 0 } };
            fsobj_id_t  fsobjid = { 0, 0 };
            struct stat stat_buf;
            ldr->loadAddress(*this)->getUuid(uuid);
            if ( dyld3::stat(ldr->path(), &stat_buf) == 0 ) { // FIXME, get inode from Loader
                fsobjid = *(fsobj_id_t*)&stat_buf.st_ino;
                fsid    = { { stat_buf.st_dev, 0 } };
            }
            kdebug_trace_dyld_image(DBG_DYLD_UUID_UNMAP_A, ldr->path(), &uuid, fsobjid, fsid, ldr->loadAddress(*this));
        }
    }

    // tell dtrace about static probes that are going away
    if ( config.syscall.dtraceUserProbesEnabled() ) {
        for ( const Loader* removeeLdr : loadersToRemove ) {
            // remove all RegisteredDOF entries for removeeLdr, and unregister it
            _loadersNeedingDOFUnregistration.erase(std::remove_if(_loadersNeedingDOFUnregistration.begin(), _loadersNeedingDOFUnregistration.end(), [&](const RegisteredDOF& entry){
                if ( entry.ldr == removeeLdr ) {
                    config.syscall.dtraceUnregisterUserProbe(entry.registrationID);
                    return true;
                }
                return false;
            }), _loadersNeedingDOFUnregistration.end());
        }
    }

    removeMissingFlatLazySymbols(loadersToRemove);

    withLoadersWriteLock(^() {
        // remove each from loaded
        for ( const Loader* removeeLoader : loadersToRemove ) {
            for ( auto it=loaded.begin(); it != loaded.end(); ++it ) {
                if ( *it == removeeLoader ) {
                    loaded.erase(it);
                    break;
                }
            }
            // remove any entries in weakDefMap
            removeDynamicDependencies(removeeLoader);
        }
    });

    // tell debugger about removed images (do last so any code run during notifiers can be debugged)
    notifyDebuggerUnload(loadersToRemove);

#if BUILDING_DYLD
    // notify any processes tracking loads in this process
    STACK_ALLOC_ARRAY(const char*, pathsBuffer, loadersToRemove.count());
    STACK_ALLOC_ARRAY(const mach_header*, mhBuffer, loadersToRemove.count());
    for ( const Loader* ldr : loadersToRemove ) {
        pathsBuffer.push_back(ldr->path());
        mhBuffer.push_back(ldr->loadAddress(*this));
    }
    notifyMonitoringDyld(true, (uint32_t)pathsBuffer.count(), &mhBuffer[0], &pathsBuffer[0]);
#endif
}


void RuntimeState::removeDynamicDependencies(const Loader* removee)
{
    // remove any dynamic dependencies that involve removee
    _dynamicReferences.erase(std::remove_if(_dynamicReferences.begin(), _dynamicReferences.end(), [&](const DynamicReference& ref) {
        return ( (ref.from == removee) || (ref.to == removee) );
    }), _dynamicReferences.end());

    // remove any trace of removee in the weakDefMap
    if ( this->weakDefMap != nullptr ) {
        const MachOAnalyzer* ma = removee->analyzer(*this);
        if ( ma->hasWeakDefs() || ma->usesWeakDefs() ) {
            const char* startAddr = (const char*)ma;
            const char* endAddr = startAddr + ma->mappedSize();

            // see if this weakDef from 'removee' is in the weakDefMap and is the impl choosen
            for ( auto& keyAndValue : *this->weakDefMap ) {
                // the impl is being unloaded, mark it unused
                if ( keyAndValue.second.targetLoader == removee )
                    keyAndValue.second.targetLoader = nullptr;

                if ( keyAndValue.first < startAddr )
                    continue;
                if ( keyAndValue.first >= endAddr )
                    continue;

                // The string key is being unloaded, replace it with a strdup
                keyAndValue.first = this->longTermAllocator.strdup(keyAndValue.first);
            }
        }
    }
}

void RuntimeState::removeLoaders(const dyld3::Array<const Loader*>& loadersToRemove)
{
    // unmap images
    for ( const Loader* removeeLoader : loadersToRemove ) {
        bool dealloc = false;
        // don't unmap images in the dyld cache
        if ( removeeLoader->dylibInDyldCache )
            continue;
        // don't unmap images dlopen()ed with RTLD_NODELETE
        if ( removeeLoader->leaveMapped )
            continue;
        if ( !removeeLoader->isPrebuilt ) {
            // also handle when RTLD_NODELETE used on later dlopen() calls
            JustInTimeLoader* jitLoader = (JustInTimeLoader*)removeeLoader;
            if ( jitLoader->shouldLeaveMapped() )
                continue;
            dealloc = true;
        }
        removeeLoader->unmap(*this);
        if ( dealloc ) {
            // stomp header of Loader so that if someone tries to re-use free'd pointer it fails
            uint32_t* p = (uint32_t*)removeeLoader;
            *p = 'zldr'; // zombie loader
            this->longTermAllocator.free((void*)removeeLoader);
        }
    }
}

void RuntimeState::setObjCNotifiers(_dyld_objc_notify_mapped mapped, _dyld_objc_notify_init init, _dyld_objc_notify_unmapped unmapped)
{
    _notifyObjCMapped   = mapped;
    _notifyObjCInit     = init;
    _notifyObjCUnmapped = unmapped;

    withLoadersReadLock(^{
        // callback about already loaded images
        size_t maxCount = this->loaded.size();
        STACK_ALLOC_ARRAY(const mach_header*, mhs, maxCount);
        STACK_ALLOC_ARRAY(const char*, paths, maxCount);
        for ( const Loader* ldr : loaded ) {
            // don't need _mutex here because this is called when process is still single threaded
            const MachOLoaded* ml = ldr->loadAddress(*this);
            if ( ldr->hasObjC ) {
                paths.push_back(ldr->path());
                mhs.push_back(ml);
            }
        }
        if ( !mhs.empty() ) {
            (*_notifyObjCMapped)((uint32_t)mhs.count(), &paths[0], &mhs[0]);
            if ( this->config.log.notifications ) {
                this->log("objc-mapped-notifier called with %ld images:\n", mhs.count());
                for ( uintptr_t i = 0; i < mhs.count(); ++i ) {
                    this->log(" objc-mapped: %p %s\n", mhs[i], paths[i]);
                }
            }
        }
    });
}

void RuntimeState::notifyObjCInit(const Loader* ldr)
{
    //this->log("objc-init-notifier checking mh=%p, path=%s, +load=%d, objcInit=%p\n", ldr->loadAddress(), ldr->path(), ldr->mayHavePlusLoad, _notifyObjCInit);
    if ( (_notifyObjCInit != nullptr) && ldr->mayHavePlusLoad ) {
        const MachOLoaded* ml  = ldr->loadAddress(*this);
        const char*        pth = ldr->path();
        dyld3::ScopedTimer timer(DBG_DYLD_TIMING_OBJC_INIT, (uint64_t)ml, 0, 0);
        if ( this->config.log.notifications )
            this->log("objc-init-notifier called with mh=%p, path=%s\n", ml, pth);
        _notifyObjCInit(pth, ml);
    }
}

void RuntimeState::addNotifyAddFunc(const Loader* callbackLoader, NotifyFunc func)
{
    _notifyAddImage.push_back(func);

    // There's no way to unregister the notifier, so make sure we never unload the client
    if ( (callbackLoader != nullptr) && !callbackLoader->neverUnload )
        _dynamicNeverUnloads.push_back(callbackLoader);
}

void RuntimeState::addNotifyRemoveFunc(const Loader* callbackLoader, NotifyFunc func)
{
    _notifyRemoveImage.push_back(func);

    // There's no way to unregister the notifier, so make sure we never unload the client
    if ( (callbackLoader != nullptr) && !callbackLoader->neverUnload )
        _dynamicNeverUnloads.push_back(callbackLoader);
}

void RuntimeState::addNotifyLoadImage(const Loader* callbackLoader, LoadNotifyFunc func)
{
    _notifyLoadImage.push_back(func);

    // There's no way to unregister the notifier, so make sure we never unload the client
    if ( (callbackLoader != nullptr) && !callbackLoader->neverUnload )
        _dynamicNeverUnloads.push_back(callbackLoader);
}

void RuntimeState::addNotifyBulkLoadImage(const Loader* callbackLoader, BulkLoadNotifier func)
{
    _notifyBulkLoadImage.push_back(func);

    // There's no way to unregister the notifier, so make sure we never unload the client
    if ( (callbackLoader != nullptr) && !callbackLoader->neverUnload )
        _dynamicNeverUnloads.push_back(callbackLoader);
}

// called during libSystem.dylib initialization
void RuntimeState::initialize()
{
#if BUILDING_DYLD
    // assign pthread_key for per-thread dlerror messages
    // NOTE: dlerror uses malloc() - not dyld's Allocator to store per-thread error messages
    this->libSystemHelpers->pthread_key_create_free(&_dlerrorPthreadKey);

    // assign pthread_key for per-thread terminators
    // Note: if a thread is terminated the value for this key is cleaned up by calling _finalizeListTLV()
    this->libSystemHelpers->pthread_key_create_thread_exit(&_tlvTerminatorsKey);

    // if images have thread locals, set them up
    for ( const Loader* ldr : this->loaded ) {
        const MachOAnalyzer* ma = ldr->analyzer(*this);
        if ( ma->hasThreadLocalVariables() )
            this->setUpTLVs(ma);
    }
#endif
}

void RuntimeState::setUpTLVs(const MachOAnalyzer* ma)
{
#if BUILDING_DYLD

#if SUPPPORT_PRE_LC_MAIN
    // Support for macOS 10.4 binaries with custom crt1.o glue and call dlopen before initializers are run
    if ( this->libSystemHelpers == nullptr )
        return;
#endif

    TLV_Info info;
    info.ma = ma;
    // Note: the space for thread local variables is allocated with
    // system malloc and freed on thread death with system free()
    if ( this->libSystemHelpers->pthread_key_create_free(&info.key) != 0 )
        halt("could not create thread local variables pthread key");
    Diagnostics                       diag;
    MachOAnalyzer::TLV_InitialContent initialContent;
    initialContent            = ma->forEachThreadLocalVariable(diag, ^(MachOAnalyzer::TLV_Thunk& slot) {
        // initialize each descriptor
        slot.thunk = this->libSystemHelpers->getTLVGetAddrFunc();
        slot.key   = info.key;
        // slot.offset remains unchanged
    });
    info.initialContentOffset = (uint32_t)initialContent.runtimeOffset;
    info.initialContentSize   = (uint32_t)initialContent.size;
    withTLVLock(^() {
        _tlvInfos.push_back(info);
    });
#endif // BUILDING_DYLD
}

// called lazily when TLV is first accessed
void* RuntimeState::_instantiateTLVs(pthread_key_t key)
{
#if BUILDING_DYLD
    // find amount to allocate and initial content
    __block const uint8_t* initialContent     = nullptr;
    __block size_t         initialContentSize = 0;
    withTLVLock(^() {
        for ( const auto& info : _tlvInfos ) {
            if ( info.key == key ) {
                initialContent     = (uint8_t*)info.ma + info.initialContentOffset;
                initialContentSize = info.initialContentSize;
            }
        }
    });

    // no thread local storage in image: should never happen
    if ( initialContent == nullptr )
        return nullptr;

    // allocate buffer and fill with template
    // Note: the space for thread local variables is allocated with system malloc
    void* buffer = this->libSystemHelpers->malloc(initialContentSize);
    memcpy(buffer, initialContent, initialContentSize);

    // set this thread's value for key to be the new buffer.
    this->libSystemHelpers->pthread_setspecific(key, buffer);

    return buffer;
#else
    return nullptr;
#endif
}

void RuntimeState::addTLVTerminationFunc(TLV_TermFunc func, void* objAddr)
{
#if BUILDING_DYLD
    // NOTE: this does not need locks because it only operates on current thread data
    TLV_TerminatorList* list = (TLV_TerminatorList*)this->libSystemHelpers->pthread_getspecific(_tlvTerminatorsKey);
    if ( list == nullptr ) {
        // Note: use system malloc because it is thread safe
        list = (TLV_TerminatorList*)this->libSystemHelpers->malloc(sizeof(TLV_TerminatorList));
        bzero(list, sizeof(TLV_TerminatorList));
        this->libSystemHelpers->pthread_setspecific(_tlvTerminatorsKey, list);
    }
    // go to end of chain
    while (list->next != nullptr)
        list = list->next;
    // make sure there is space to add another element
    if ( list->count == 7 ) {
        // if list is full, add a chain
        TLV_TerminatorList* nextList = (TLV_TerminatorList*)this->libSystemHelpers->malloc(sizeof(TLV_TerminatorList));
        bzero(nextList, sizeof(TLV_TerminatorList));
        list->next = nextList;
        list = nextList;
    }
    list->elements[list->count++] = { func, objAddr };
#endif
}

void RuntimeState::TLV_TerminatorList::reverseWalkChain(void (^visit)(TLV_TerminatorList*))
{
    if ( this->next != nullptr )
        this->next->reverseWalkChain(visit);
    visit(this);
}

void RuntimeState::_finalizeListTLV(void* l)
{
#if BUILDING_DYLD
    // on entry, libc has set the TSD slot to nullptr and passed us the previous value
    TLV_TerminatorList* list = (TLV_TerminatorList*)l;
    // call term functions in reverse order of construction
    list->reverseWalkChain(^(TLV_TerminatorList* chain) {
        for ( uintptr_t i = chain->count; i > 0; --i ) {
            const TLV_Terminator& entry = chain->elements[i - 1];
            if ( entry.termFunc != nullptr )
                (*entry.termFunc)(entry.objAddr);

            // If a new tlv was added via tlv_atexit during the termination function just called, then we need to immediately destroy it
            TLV_TerminatorList* newlist = (TLV_TerminatorList*)(this->libSystemHelpers->pthread_getspecific(_tlvTerminatorsKey));
            if ( newlist != nullptr ) {
                // Set the list to NULL so that if yet another tlv is registered, we put it in a new list
                this->libSystemHelpers->pthread_setspecific(_tlvTerminatorsKey, nullptr);
                this->_finalizeListTLV(newlist);
            }
        }
    });

    // free entire chain
    list->reverseWalkChain(^(TLV_TerminatorList* chain) {
        this->libSystemHelpers->free(chain);
    });
#endif
}

// <rdar://problem/13741816>
// called by exit() before it calls cxa_finalize() so that thread_local
// objects are destroyed before global objects.
// Note this is only called on macOS, and by libc.
// iOS only destroys tlv's when each thread is destroyed and libpthread calls
// tlv_finalize as that is the pointer we provided when we created the key
void RuntimeState::exitTLV()
{
#if BUILDING_DYLD
    Vector<TLV_Terminator>* list = (Vector<TLV_Terminator>*)this->libSystemHelpers->pthread_getspecific(_tlvTerminatorsKey);
    if ( list != nullptr ) {
        // detach storage from thread while freeing it
        this->libSystemHelpers->pthread_setspecific(_tlvTerminatorsKey, nullptr);
        // Note, if new thread locals are added to our during this termination,
        // they will be on a new list, but the list we have here
        // is one we own and need to destroy it
        this->_finalizeListTLV(list);
    }
#endif
}


void RuntimeState::buildAppPrebuiltLoaderSetPath(bool createDirs)
{
    char prebuiltLoaderSetPath[PATH_MAX];

    if ( const char* closureDir = config.process.environ("DYLD_CLOSURE_DIR"); config.security.internalInstall && (closureDir != nullptr) ) {
        ::strlcpy(prebuiltLoaderSetPath, closureDir, PATH_MAX);
    }
    else if ( const char* homeDir = config.process.environ("HOME") ) {

        // First check if the raw path looks likely to be containerized.  This avoids sandbox violations
        // when passed a non-containerized HOME
        bool isMaybeContainerized = false;
        if ( config.syscall.isMaybeContainerized(homeDir) ) {
            isMaybeContainerized = true;
            // containerized check needs to check the realpath
            if ( !config.syscall.realpath(homeDir, prebuiltLoaderSetPath) )
                return;
        }

        // make $HOME/Library/Caches/com.apple.dyld/
        strlcat(prebuiltLoaderSetPath, "/Library/Caches/com.apple.dyld/", PATH_MAX);

        if ( isMaybeContainerized && config.syscall.isContainerized(prebuiltLoaderSetPath) ) {
            // make sure dir structure exist
            if ( createDirs && !config.syscall.dirExists(prebuiltLoaderSetPath) ) {
                if ( !config.syscall.mkdirs(prebuiltLoaderSetPath) )
                    return;
            }
            // containerized closures go into $HOME/Library/Caches/com.apple.dyld/<prog-name>.dyld4
            ::strlcat(prebuiltLoaderSetPath, config.process.progname, PATH_MAX);
            ::strlcat(prebuiltLoaderSetPath, ".dyld4", PATH_MAX);
        }
        else if ( config.security.internalInstall ) {
#if BUILDING_DYLD && !TARGET_OS_OSX
            // On embedded, only save closure file if app is containerized, unless DYLD_USE_CLOSURES forces
            if ( config.process.environ("DYLD_USE_CLOSURES") == nullptr )
                return;
#endif

            // non-containerized apps share same $HOME, so need extra path components
            // $HOME/Library/Caches/com.apple.dyld/<prog-name>/<cd-hash>-<path-hash>.dyld4
            ::strlcat(prebuiltLoaderSetPath, config.process.progname, PATH_MAX);
            ::strlcat(prebuiltLoaderSetPath, "/", PATH_MAX);
            if ( createDirs && !config.syscall.dirExists(prebuiltLoaderSetPath) ) {
                if ( !config.syscall.mkdirs(prebuiltLoaderSetPath) )
                    return;
            }
            // use cdHash passed by kernel to identify binary
            if ( const char* mainExeCdHashStr = config.process.appleParam("executable_cdhash") ) {
                ::strlcat(prebuiltLoaderSetPath, mainExeCdHashStr, PATH_MAX);
                ::strlcat(prebuiltLoaderSetPath, "-", PATH_MAX);
            }
            // append path hash so same binary in to locations use differnt PBLS
            uint64_t pathHash = std::hash<std::string_view>()(config.process.mainExecutablePath);
            char pathHex[32];
            char* p = pathHex;
            for (int i=0; i < 8; ++i) {
                uint8_t byte = pathHash & 0xFF;
                Loader::appendHexByte(byte, p);
                pathHash = pathHash >> 8;
            }
            *p = '\0';
            ::strlcat(prebuiltLoaderSetPath, pathHex, PATH_MAX);
            ::strlcat(prebuiltLoaderSetPath, ".dyld4", PATH_MAX);
        }
    }
    else {
        return; // no env var, so no place for closure file
    }
    _processPrebuiltLoaderSetPath = this->longTermAllocator.strdup(prebuiltLoaderSetPath);
}

bool RuntimeState::buildBootToken(dyld3::Array<uint8_t>& bootToken) const
{
    // <rdar://60333505> bootToken is a concat of: 1) boot-hash of app, 2) dyld's uuid, 3) hash of path to main program
    uint8_t  programHash[128];
    unsigned programHashLen = 0;
    if ( const char* bootHashString = config.process.appleParam("executable_boothash") ) {
        if ( hexStringToBytes(bootHashString, programHash, sizeof(programHash), programHashLen) ) {
            // cdhash of main executable
            for ( unsigned i = 0; i < programHashLen; ++i )
                bootToken.push_back(programHash[i]);
            // dyld'd uuid
            uuid_t dyldUuid;
            if ( __dso_handle.getUuid(dyldUuid) ) {
                for ( size_t i = 0; i < sizeof(dyldUuid); ++i )
                    bootToken.push_back(dyldUuid[i]);
            }
            // hash of path to app
            uint64_t pathHash = std::hash<std::string_view>()(config.process.mainExecutablePath);
            for (int i=0; i < 8; ++i) {
                uint8_t byte = pathHash & 0xFF;
                bootToken.push_back(byte);
                pathHash = pathHash >> 8;
            }
            return true;
        }
    }
    return false;
}

bool RuntimeState::fileAlreadyHasBootToken(const char* path, const Array<uint8_t>& bootToken) const
{
    // compare boot token to one saved on PrebuiltLoaderSet file
    STACK_ALLOC_ARRAY(uint8_t, fileToken, kMaxBootTokenSize);
    if ( !config.syscall.getFileAttribute(_processPrebuiltLoaderSetPath, DYLD_CLOSURE_XATTR_NAME, fileToken) )
        return false;
    if ( fileToken.count() != bootToken.count() )
        return false;
    if ( ::memcmp(bootToken.begin(), fileToken.begin(), bootToken.count()) != 0 )
        return false;
    return true;
}

#if ( BUILDING_DYLD && !TARGET_OS_SIMULATOR ) || BUILDING_CLOSURE_UTIL
void RuntimeState::loadAppPrebuiltLoaderSet()
{
    // don't look for file attribute if file does not exist
    if ( !config.syscall.fileExists(_processPrebuiltLoaderSetPath) )
        return;

    // get boot token for this process
    STACK_ALLOC_ARRAY(uint8_t, bootToken, kMaxBootTokenSize);
    if ( !this->buildBootToken(bootToken) ) {
#if BUILDING_DYLD
        if ( config.log.loaders )
            this->log("did not look for saved PrebuiltLoaderSet because main executable is not codesigned\n");
#endif
        return;
    }

    // compare boot token to one saved on PrebuiltLoaderSet file
    if ( !fileAlreadyHasBootToken(_processPrebuiltLoaderSetPath, bootToken) ) {
#if BUILDING_DYLD
        if ( config.log.loaders )
            this->log("existing PrebuiltLoaderSet file not used because boot-token differs\n");
#endif
        return;
    }

    // boot token matches, so we can use app PrebuiltLoaderSet file
    Diagnostics mapDiag;
    _processPrebuiltLoaderSet = (PrebuiltLoaderSet*)config.syscall.mapFileReadOnly(mapDiag, _processPrebuiltLoaderSetPath);

    // make sure there is enough space for the state array (needed during recursive isValid())
    if ( _processPrebuiltLoaderSet != nullptr ) {
        allocateProcessArrays(_processPrebuiltLoaderSet->loaderCount());
        _processLoadedAddressArray[0] = config.process.mainExecutable;
    }

    // verify it is still valid (no roots installed or OS update)
    if ( (_processPrebuiltLoaderSet != nullptr) && !_processPrebuiltLoaderSet->isValid(*this) ) {
        config.syscall.unmapFile((void*)_processPrebuiltLoaderSet, _processPrebuiltLoaderSet->size());
        _processPrebuiltLoaderSet = nullptr;
        return;
    }
}

bool RuntimeState::saveAppPrebuiltLoaderSet(const PrebuiltLoaderSet* toSaveLoaderSet) const
{
    // get boot token for this process
    STACK_ALLOC_ARRAY(uint8_t, bootToken, kMaxBootTokenSize);
    if ( !this->buildBootToken(bootToken) ) {
#if BUILDING_DYLD
        if ( config.log.loaders )
            this->log("could not save PrebuiltLoaderSet because main executable is not codesigned\n");
#endif
        return false;
    }

    // verify there is a location to save
    if ( _processPrebuiltLoaderSetPath == nullptr ) {
#if BUILDING_DYLD
        if ( config.log.loaders )
            this->log("no path to save PrebuiltLoaderSet file\n");
#endif
        return false;
    }

    // see if there already is a closure file on disk
    Diagnostics mapDiag;
    if ( const PrebuiltLoaderSet* existingLoaderSet = (PrebuiltLoaderSet*)config.syscall.mapFileReadOnly(mapDiag, _processPrebuiltLoaderSetPath) ) {
        bool canReuse = (existingLoaderSet->size() == toSaveLoaderSet->size()) && (::memcmp(existingLoaderSet, toSaveLoaderSet, existingLoaderSet->size()) == 0);
        bool doReuse = false;
        if ( canReuse ) {
            // closure file already exists and has same content, so re-use file by altering boot-token
            if ( fileAlreadyHasBootToken(_processPrebuiltLoaderSetPath, bootToken) ) {
                doReuse = true;
    #if BUILDING_DYLD
                if ( config.log.loaders )
                    this->log("PrebuiltLoaderSet already saved as file '%s'\n", _processPrebuiltLoaderSetPath);
    #endif
            }
            else {
    #if BUILDING_DYLD
                if ( config.log.loaders )
                    this->log("updating boot attribute on existing PrebuiltLoaderSet file '%s'\n", _processPrebuiltLoaderSetPath);
    #endif
                doReuse = config.syscall.setFileAttribute(_processPrebuiltLoaderSetPath, DYLD_CLOSURE_XATTR_NAME, bootToken);
            }
        }
        config.syscall.unmapFile((void*)existingLoaderSet, existingLoaderSet->size());
        if ( doReuse ) {
            return true;
        }
        // PrebuiltLoaderSet has changed so delete old file
        config.syscall.unlink(_processPrebuiltLoaderSetPath);
        // no need to check unlink success because saveFileWithAttribute() will overwrite if needed
    #if BUILDING_DYLD
        if ( config.log.loaders )
            this->log("deleting existing out of date PrebuiltLoaderSet file '%s'\n", _processPrebuiltLoaderSetPath);
    #endif
    }

    // write PrebuiltLoaderSet to disk
    Diagnostics saveDiag;
    if ( config.syscall.saveFileWithAttribute(saveDiag, _processPrebuiltLoaderSetPath, toSaveLoaderSet, toSaveLoaderSet->size(), DYLD_CLOSURE_XATTR_NAME, bootToken) ) {
    #if BUILDING_DYLD
        if ( config.log.loaders )
            this->log("wrote PrebuiltLoaderSet to file '%s'\n", _processPrebuiltLoaderSetPath);
    #endif
        return true;
    }
    else {
    #if BUILDING_DYLD
        if ( config.log.loaders )
            this->log("tried but failed (%s) to write PrebuiltLoaderSet to file '%s'\n", saveDiag.errorMessageCStr(), _processPrebuiltLoaderSetPath);
    #endif
    }
    return false;
}
#endif // BUILDING_DYLD && !TARGET_OS_SIMULATOR


#if !BUILDING_DYLD
// called by dyld_closure_util
void RuntimeState::setProcessPrebuiltLoaderSet(const PrebuiltLoaderSet* appPBLS)
{
    _processPrebuiltLoaderSet     = appPBLS;
    _processDylibStateArray       = (uint8_t*)calloc(appPBLS->loaderCount(), 1);
    _processLoadedAddressArray    = (const MachOLoaded**)calloc(appPBLS->loaderCount(), sizeof(MachOLoaded*));
    resetCachedDylibsArrays();
}

void RuntimeState::resetCachedDylibsArrays()
{
    _cachedDylibsPrebuiltLoaderSet = (PrebuiltLoaderSet*)(config.dyldCache.addr->header.dylibsPBLSetAddr + config.dyldCache.slide);
    _cachedDylibsStateArray        = (uint8_t*)this->longTermAllocator.malloc(_cachedDylibsPrebuiltLoaderSet->loaderCount());
    bzero(_cachedDylibsStateArray, _cachedDylibsPrebuiltLoaderSet->loaderCount());
}
#endif // !BUILDING_DYLD

const PrebuiltLoader* RuntimeState::findPrebuiltLoader(const char* path) const
{
    // see if path is a dylib in dyld cache
    uint32_t dylibIndex;
    if ( (_cachedDylibsPrebuiltLoaderSet != nullptr) && config.dyldCache.addr->hasImagePath(path, dylibIndex) ) {
        const PrebuiltLoader* ldr = this->_cachedDylibsPrebuiltLoaderSet->atIndex(dylibIndex);
        if ( ldr->isValid(*this) )
            return ldr;
    }
#if BUILDING_DYLD && !TARGET_OS_SIMULATOR
    // see if path is in app PrebuiltLoaderSet
    if ( this->_processPrebuiltLoaderSet != nullptr ) {
        if ( const PrebuiltLoader* ldr = _processPrebuiltLoaderSet->findLoader(path) ) {
            if ( ldr->isValid(*this) )
                return ldr;
        }
    }
#endif
    return nullptr;
}

// When a root of an OS program is installed, the PrebuiltLoaderSet for it in the dyld cache is invalid.
// This setting lets dyld build a new PrebuiltLoaderSet for that OS program that overrides the one in the cache.
bool RuntimeState::allowOsProgramsToSaveUpdatedClosures() const
{
    // until a better security policy is worked out, don't let local closure files override closures in dyld cache
    return false;
}

bool RuntimeState::allowNonOsProgramsToSaveUpdatedClosures() const
{
    // on embedded, all 3rd party apps can build closures
    switch ( config.process.platform ) {
        case Platform::iOS:
#if BUILDING_DYLD && TARGET_OS_OSX && __arm64__
            return false; // don't save closures for iPad apps running on Apple Silicon
#else
            return true;
#endif
        case Platform::tvOS:
        case Platform::watchOS:
            return true;
        default:
            break;
    }

    // need cdhash of executable to build closure
    if ( config.process.appleParam("executable_cdhash") == nullptr )
        return false;

    // <rdar://74910825> disable macOS closure saving
    return false;
}

void RuntimeState::initializeClosureMode()
{
    // get pointers info dyld cache for cached dylibs PrebuiltLoaders
    _cachedDylibsStateArray        = nullptr;
    _cachedDylibsPrebuiltLoaderSet = nullptr;
    if ( (config.dyldCache.addr != nullptr) && (config.dyldCache.addr->header.mappingOffset >= 0x170) ) {
        const PrebuiltLoaderSet* cdpbls = (PrebuiltLoaderSet*)(config.dyldCache.addr->header.dylibsPBLSetAddr + config.dyldCache.slide);
        if ( cdpbls->validHeader(*this) ) {
            // only use PrebuiltLoaders from the dyld cache if they have the same version hash as this dyld
            _cachedDylibsPrebuiltLoaderSet = cdpbls;
            _cachedDylibsStateArray        = (uint8_t*)this->longTermAllocator.malloc(_cachedDylibsPrebuiltLoaderSet->loaderCount());
            bzero(_cachedDylibsStateArray, _cachedDylibsPrebuiltLoaderSet->loaderCount());
        }
    }

    _saveAppClosureFile           = false;
    _processPrebuiltLoaderSetPath = nullptr;
    _processDylibStateArray       = nullptr;
    _processLoadedAddressArray    = nullptr;

    // determine policy for using PrebuiltLoaderSets
    const PrebuiltLoaderSet* cachePBLS = nullptr;
    bool isOsProgram              = false;
    bool lookForPBLSetInDyldCache = false;
    bool lookForPBLSetOnDisk      = false;
    bool mayBuildAndSavePBLSet    = false;
    bool requirePBLSet            = false;
    if ( config.dyldCache.addr == nullptr ) {
#if BUILDING_DYLD
        if ( config.log.loaders )
            this->log("PrebuiltLoaders not being used because there is no dyld shared cache\n");
#endif
    }
    else if ( config.pathOverrides.dontUsePrebuiltForApp() ) {
#if BUILDING_DYLD
        if ( config.log.loaders )
            this->log("PrebuiltLoaders not being used because DYLD_ env vars are set\n");
#endif
    }
    else if ( (_cachedDylibsPrebuiltLoaderSet != nullptr) && (_cachedDylibsStateArray != nullptr) ) {
        // at this point we know we have a new dyld cache that contains PrebuiltLoaders
        cachePBLS                = config.dyldCache.addr->findLaunchLoaderSet(config.process.mainExecutablePath); // optimistically check cache
        isOsProgram              = (cachePBLS != nullptr) || config.dyldCache.addr->hasLaunchLoaderSetWithCDHash(config.process.appleParam("executable_cdhash"));
        lookForPBLSetInDyldCache = true;
        lookForPBLSetOnDisk      = isOsProgram ? this->allowOsProgramsToSaveUpdatedClosures() : this->allowNonOsProgramsToSaveUpdatedClosures();
        mayBuildAndSavePBLSet    = lookForPBLSetOnDisk;
        requirePBLSet            = false;

        if ( config.security.internalInstall ) {
            // check for env vars that forces different behavior
            //    default              -->  Look for PrebuiltLoaderSet and use if valid, otherwise JIT
            //    DYLD_USE_CLOSURES=0  -->  JIT mode for main executable (even OS programs)
            //    DYLD_USE_CLOSURES=1  -->  JIT mode for main executable, and save a PrebuiltLoaderSet
            //    DYLD_USE_CLOSURES=2  -->  require a PrebuiltLoaderSet or fail launch
            //
            if ( const char* closureMode = config.process.environ("DYLD_USE_CLOSURES") ) {
                if ( ::strcmp(closureMode, "0") == 0 ) {
                    lookForPBLSetInDyldCache = false;
                    lookForPBLSetOnDisk      = false;
                    mayBuildAndSavePBLSet    = false;
                    requirePBLSet            = false;
                    cachePBLS                = nullptr;
                    _cachedDylibsPrebuiltLoaderSet = nullptr;
                }
                else if ( ::strcmp(closureMode, "1") == 0 ) {
                    lookForPBLSetInDyldCache = false;
                    lookForPBLSetOnDisk      = false;
                    mayBuildAndSavePBLSet    = true;
                    requirePBLSet            = false;
                    if ( !this->allowNonOsProgramsToSaveUpdatedClosures() ) {
                        mayBuildAndSavePBLSet = false;
#if BUILDING_DYLD
                        if ( config.log.loaders )
                            this->log("PrebuiltLoaders cannot be used with unsigned or old format programs\n");
#endif
                    }
                }
                else if ( ::strcmp(closureMode, "2") == 0 ) {
                    lookForPBLSetInDyldCache = true;
                    lookForPBLSetOnDisk      = true;
                    mayBuildAndSavePBLSet    = false;
                    requirePBLSet            = true;
                    if ( !this->allowNonOsProgramsToSaveUpdatedClosures() ) {
                        mayBuildAndSavePBLSet = false;
#if BUILDING_DYLD
                        if ( config.log.loaders )
                            this->log("PrebuiltLoaders cannot be used with unsigned or old format programs\n");
#endif
                    }
                }
            }
        }
    }

    // first check for closure file on disk
    if ( lookForPBLSetOnDisk ) {
        // build path to where on-disk closure file should be
        this->buildAppPrebuiltLoaderSetPath(false);

        // don't try to build and save closure if no place to save it
        if ( _processPrebuiltLoaderSetPath == nullptr )
            mayBuildAndSavePBLSet = false;

#if ( BUILDING_DYLD && !TARGET_OS_SIMULATOR ) || BUILDING_CLOSURE_UTIL
        // load closure file is possible
        if ( _processPrebuiltLoaderSetPath != nullptr )
            this->loadAppPrebuiltLoaderSet();
#endif
    }

    // if no closure file found so far, look in dyld cache
    if ( (_processPrebuiltLoaderSet == nullptr) && ( cachePBLS != nullptr) ) {
        // alloc state array (needed during recursive isValid())
        allocateProcessArrays(cachePBLS->loaderCount());
        _processLoadedAddressArray[0] = config.process.mainExecutable;

        const PrebuiltLoader* mainPbl = cachePBLS->atIndex(0);
        if ( config.log.loaders )
            this->log("PrebuiltLoader %p found for %s in the dyld cache\n", mainPbl, config.process.mainExecutablePath);

        // check against the cdHash the kernel passed down
        bool cdHashMatchesRecorded = false;
        if ( const char* mainExeCdHashStr = config.process.appleParam("executable_cdhash") ) {
            uint8_t  mainExecutableCDHash[20];
            unsigned bufferLenUsed;
            if ( hexStringToBytes(mainExeCdHashStr, mainExecutableCDHash, 20, bufferLenUsed) )
                cdHashMatchesRecorded = mainPbl->recordedCdHashIs(mainExecutableCDHash);
        }
        if ( !cdHashMatchesRecorded ) {
            if ( config.log.loaders )
                this->log("PrebuiltLoader %p not used because cdHash does not match\n", mainPbl);
        }
        else {
            // set this before isValid(), so depedent PrebuilLoaders can be found
            _processPrebuiltLoaderSet = cachePBLS;
            if ( !_processPrebuiltLoaderSet->isValid(*this) ) {
                if ( config.log.loaders )
                    this->log("PrebuiltLoader %p not used because Loader for %s is invalid\n", cachePBLS, mainPbl->path());
                // something has changed in the file system, don't use PrebuiltLoader, make a JustInTimeLoader for main executable
                _processPrebuiltLoaderSet = nullptr;
            }
        }
    }

    // if we don't have a PrebuiltLoaderSet, then remember to save one later
    if ( _processPrebuiltLoaderSet == nullptr ) {
        _saveAppClosureFile = mayBuildAndSavePBLSet;  // build path to where on-disk closure file should be

        if ( _saveAppClosureFile )
            this->buildAppPrebuiltLoaderSetPath(true);
    }

    // fail if no PrebuiltLoaderSet, but one is required
    _failIfCouldBuildAppClosureFile = false;
    if ( requirePBLSet && (_processPrebuiltLoaderSet == nullptr) && (config.dyldCache.addr != nullptr) && mayBuildAndSavePBLSet && (_processPrebuiltLoaderSetPath != nullptr) ) {
        _failIfCouldBuildAppClosureFile = true;
#if BUILDING_DYLD
        if ( config.log.loaders )
            this->log("PrebuiltLoaderSet required for '%s' but not found at '%s'\n", config.process.progname, _processPrebuiltLoaderSetPath);
#endif

    }
}

void RuntimeState::allocateProcessArrays(uintptr_t count)
{
    _processDylibStateArray    = (uint8_t*)this->longTermAllocator.malloc(count);
    _processLoadedAddressArray = (const MachOLoaded**)this->longTermAllocator.malloc(count*sizeof(MachOLoaded*));
    bzero(_processDylibStateArray, count);
    bzero(_processLoadedAddressArray, count*sizeof(MachOLoaded*));
}

bool RuntimeState::inPrebuiltLoader(const void* p, size_t len) const
{
    if ( (_cachedDylibsPrebuiltLoaderSet != nullptr) && _cachedDylibsPrebuiltLoaderSet->contains(p, len) )
        return true;
    if ( (_processPrebuiltLoaderSet != nullptr) && _processPrebuiltLoaderSet->contains(p, len) )
        return true;
    return false;
}

void RuntimeState::takeLockBeforeFork()
{
#if BUILDING_DYLD
    // We need to lock before we fork() as os_unfair_recursive_lock_unlock_forked_child() asserts that the lock is taken,
    // before then doing the reset
    if ( this->libSystemHelpers != nullptr ) {
        this->libSystemHelpers->os_unfair_recursive_lock_lock_with_options(&_locks.loadersLock, OS_UNFAIR_LOCK_NONE);
        this->libSystemHelpers->os_unfair_recursive_lock_lock_with_options(&_locks.notifiersLock, OS_UNFAIR_LOCK_NONE);
        this->libSystemHelpers->os_unfair_recursive_lock_lock_with_options(&_locks.tlvInfosLock, OS_UNFAIR_LOCK_NONE);
        // FIXME: logSerializer
    }
#endif
}

void RuntimeState::releaseLockInForkParent()
{
#if BUILDING_DYLD
    // This is on the parent side after fork().  We just to an unlock to undo the lock we did before form
    if ( this->libSystemHelpers != nullptr ) {
        this->libSystemHelpers->os_unfair_recursive_lock_unlock(&_locks.loadersLock);
        this->libSystemHelpers->os_unfair_recursive_lock_unlock(&_locks.notifiersLock);
        this->libSystemHelpers->os_unfair_recursive_lock_unlock(&_locks.tlvInfosLock);
        // FIXME: logSerializer
    }
#endif
}

void RuntimeState::resetLockInForkChild()
{
#if BUILDING_DYLD
    // This is the child side after fork().  The locks are all taken, and will be reset to their initial state
    if ( (this->libSystemHelpers != nullptr) && (this->libSystemHelpers->version() >= 2) ) {
        this->libSystemHelpers->os_unfair_recursive_lock_unlock_forked_child(&_locks.loadersLock);
        this->libSystemHelpers->os_unfair_recursive_lock_unlock_forked_child(&_locks.notifiersLock);
        this->libSystemHelpers->os_unfair_recursive_lock_unlock_forked_child(&_locks.tlvInfosLock);
        // FIXME: logSerializer
    }
#endif
}

void RuntimeState::takeDlopenLockBeforeFork()
{
#if BUILDING_DYLD
    // We need to lock before we fork() as os_unfair_recursive_lock_unlock_forked_child() asserts that the lock is taken,
    // before then doing the reset
    if ( this->libSystemHelpers != nullptr ) {
        this->libSystemHelpers->os_unfair_recursive_lock_lock_with_options(&_locks.apiLock, OS_UNFAIR_LOCK_NONE);
    }
#endif
}

void RuntimeState::releaseDlopenLockInForkParent()
{
#if BUILDING_DYLD
    // This is on the parent side after fork().  We just to an unlock to undo the lock we did before form
    if ( this->libSystemHelpers != nullptr ) {
        this->libSystemHelpers->os_unfair_recursive_lock_unlock(&_locks.apiLock);
        // FIXME: logSerializer
    }
#endif
}

void RuntimeState::resetDlopenLockInForkChild()
{
#if BUILDING_DYLD
    // This is the child side after fork().  The locks are all taken, and will be reset to their initial state
    if ( (this->libSystemHelpers != nullptr) && (this->libSystemHelpers->version() >= 2) ) {
        this->libSystemHelpers->os_unfair_recursive_lock_unlock_forked_child(&_locks.apiLock);
        // FIXME: logSerializer
    }
#endif
}


//
// MARK: --- DyldCacheDataConstLazyScopedWriter methods ---
//

DyldCacheDataConstLazyScopedWriter::DyldCacheDataConstLazyScopedWriter(RuntimeState& state)
    : _state(state)
    , _wasMadeWritable(false)
{
}

DyldCacheDataConstLazyScopedWriter::~DyldCacheDataConstLazyScopedWriter()
{
    if ( _wasMadeWritable )
        _state.config.dyldCache.makeDataConstWritable(_state.config.log, _state.config.syscall, false);
}

void DyldCacheDataConstLazyScopedWriter::makeWriteable() const
{
    if ( _wasMadeWritable )
        return;
    if ( !_state.config.process.enableDataConst )
        return;
    if ( _state.config.dyldCache.addr == nullptr )
        return;
    _wasMadeWritable = true;
    _state.config.dyldCache.makeDataConstWritable(_state.config.log, _state.config.syscall, true);
}

//
// MARK: --- DyldCacheDataConstScopedWriter methods ---
//

DyldCacheDataConstScopedWriter::DyldCacheDataConstScopedWriter(RuntimeState& state)
    : DyldCacheDataConstLazyScopedWriter(state)
{
    makeWriteable();
}


} // namespace
