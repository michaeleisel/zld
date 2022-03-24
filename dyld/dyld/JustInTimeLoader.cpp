/*
 * Copyright (c) 2019-2020 Apple Inc. All rights reserved.
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

#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "Loader.h"
#include "JustInTimeLoader.h"
#include "MachOAnalyzer.h"
#include "DyldProcessConfig.h"
#include "DyldRuntimeState.h"
#include "DebuggerSupport.h"

using dyld3::MachOAnalyzer;
using dyld3::MachOFile;

namespace dyld4 {

//////////////////////// "virtual" functions /////////////////////////////////

const MachOLoaded* JustInTimeLoader::loadAddress(RuntimeState&) const
{
    return mappedAddress;
}

const char* JustInTimeLoader::path() const
{
    return this->pathOffset ? ((char*)this + this->pathOffset) : nullptr;
}

bool JustInTimeLoader::contains(RuntimeState& state, const void* addr, const void** segAddr, uint64_t* segSize, uint8_t* segPerms) const
{
    if ( addr < this->mappedAddress )
        return false;

    __block bool         result     = false;
    const MachOAnalyzer* ma         = (const MachOAnalyzer*)this->mappedAddress;
    uint64_t             vmTextAddr = ma->preferredLoadAddress();
    uint64_t             slide      = (uintptr_t)ma - vmTextAddr;
    uint64_t             targetAddr = (uint64_t)addr;
    ma->forEachSegment(^(const MachOAnalyzer::SegmentInfo& info, bool& stop) {
        if ( ((info.vmAddr + slide) <= targetAddr) && (targetAddr < (info.vmAddr + slide + info.vmSize)) ) {
            *segAddr  = (void*)(info.vmAddr + slide);
            *segSize  = info.vmSize;
            *segPerms = info.protections;
            result    = true;
            stop      = true;
        }
    });
    return result;
}

bool JustInTimeLoader::matchesPath(const char* path) const
{
    if ( strcmp(path, this->path()) == 0 )
        return true;
    if ( this->altInstallName ) {
        if ( strcmp(path, this->analyzer()->installName()) == 0 )
            return true;
    }
    return false;
}

FileID JustInTimeLoader::fileID() const
{
    return this->fileIdent;
}

const Loader::DylibPatch* JustInTimeLoader::makePatchTable(RuntimeState& state, uint32_t indexOfOverriddenCachedDylib) const
{
    static const bool extra = false;

    const DyldSharedCache* dyldCache = state.config.dyldCache.addr;
    assert(dyldCache != nullptr);

    if ( extra )
        state.log("Found %s overrides dyld cache index 0x%04X\n", this->path(), indexOfOverriddenCachedDylib);
    uint32_t patchCount = dyldCache->patchableExportCount(indexOfOverriddenCachedDylib);
    if ( patchCount != 0 ) {
        const uint8_t*  thisAddress = (uint8_t*)(this->loadAddress(state));
        DylibPatch*     table       = (DylibPatch*)state.longTermAllocator.malloc(sizeof(DylibPatch) * (patchCount + 1));;
        __block uint32_t patchIndex = 0;
        dyldCache->forEachPatchableExport(indexOfOverriddenCachedDylib, ^(uint32_t dylibVMOffsetOfImpl, const char* exportName) {
            Diagnostics    exportDiag;
            ResolvedSymbol foundSymbolInfo;
            if ( this->hasExportedSymbol(exportDiag, state, exportName, staticLink, &foundSymbolInfo) ) {
                if ( extra )
                    state.log("   will patch cache uses of '%s'\n", exportName);
                uint8_t*  newImplAddress = (uint8_t*)(foundSymbolInfo.targetLoader->loadAddress(state)) + foundSymbolInfo.targetRuntimeOffset;
                // note: we are saving a signed 64-bit offset to the impl.  This is to support re-exported symbols
                table[patchIndex].overrideOffsetOfImpl = newImplAddress - thisAddress;
            }
            else {
                if ( extra )
                    state.log("   override missing '%s', so uses will be patched to NULL\n", exportName);
                table[patchIndex].overrideOffsetOfImpl = 0;
            }
            ++patchIndex;
        });
        // mark end of table
        table[patchIndex].overrideOffsetOfImpl = -1;
        // record in Loader
        return table;
    }

    return nullptr;
}

void JustInTimeLoader::loadDependents(Diagnostics& diag, RuntimeState& state, const LoadOptions& options)
{
    if ( dependentsSet )
        return;

    // add first level of dependents
    const MachOAnalyzer* ma       = (MachOAnalyzer*)this->mappedAddress;
    __block int          depIndex = 0;
    ma->forEachDependentDylib(^(const char* loadPath, bool isWeak, bool isReExport, bool isUpward, uint32_t compatVersion, uint32_t curVersion, bool& stop) {
        if ( isUpward )
            dependentKind(depIndex) = DependentKind::upward;
        else if ( isReExport )
            dependentKind(depIndex) = DependentKind::reexport;
        else if ( isWeak )
            dependentKind(depIndex) = DependentKind::weakLink;
        else if ( !this->allDepsAreNormal )
            dependentKind(depIndex) = DependentKind::normal;
        const Loader* depLoader = nullptr;
        // for absolute paths, do a quick check if this is already loaded with exact match
        if ( loadPath[0] == '/' ) {
            for ( const Loader* ldr : state.loaded ) {
                if ( ldr->matchesPath(loadPath) ) {
                    depLoader = ldr;
                    break;
                }
            }
        }
        if ( depLoader == nullptr ) {
            // first load, so do full search
            LoadChain   nextChain { options.rpathStack, this };
            Diagnostics depDiag;
            LoadOptions depOptions  = options;
            depOptions.rpathStack   = &nextChain;
            depOptions.canBeMissing = isWeak;
            depLoader               = options.finder ? options.finder(depDiag, state.config.process.platform, loadPath, depOptions) : getLoader(depDiag, state, loadPath, depOptions);
            if ( depDiag.hasError() ) {
                if ( depDiag.errorMessageContains("dylib not found") )
                    diag.error("Library not loaded: %s\n  Referenced from: %s\n  Reason: image not found", loadPath, this->path());
                else
                    diag.error("Library not loaded: %s\n  Referenced from: %s\n  Reason: %s", loadPath, this->path(), depDiag.errorMessageCStr());
#if BUILDING_DYLD
                if ( options.launching )
                    state.setLaunchMissingDylib(loadPath, this->path());
#endif
                stop = true;
            }
        }
        dependents[depIndex] = (Loader*)depLoader;
        depIndex++;
    });
    dependentsSet = true;
    if ( diag.hasError() )
        return;

    // breadth first recurse
    LoadChain   nextChain { options.rpathStack, this };
    LoadOptions depOptions = options;
    depOptions.rpathStack  = &nextChain;
    for ( depIndex = 0; depIndex < this->depCount; ++depIndex ) {
        if ( Loader* depLoader = dependents[depIndex] ) {
            depLoader->loadDependents(diag, state, depOptions);
        }
    }

    // if this image overrides something in the dyld cache, build a table of its patches for use by other dylibs later
    if ( this->overridesCache ) {
        this->overridePatches = makePatchTable(state, this->overrideIndex);

        // Also build patches for overrides of unzippered twins
        // The above case handled an iOSMac dylib rooting an iOSMac unzippered twin.  This handles the iOSMac dylib
        // overriding the macOS unzippered twin
        this->overridePatchesCatalystMacTwin = nullptr;
        if ( state.config.process.catalystRuntime ) {
            // Find the macOS twin overridden index
            uint16_t macOSTwinIndex = Loader::indexOfUnzipperedTwin(state, this->overrideIndex);
            if ( macOSTwinIndex != kNoUnzipperedTwin )
                this->overridePatchesCatalystMacTwin = makePatchTable(state, macOSTwinIndex);
        }
    }
}

uint32_t JustInTimeLoader::dependentCount() const
{
    return this->depCount;
}

JustInTimeLoader::DependentKind& JustInTimeLoader::dependentKind(uint32_t depIndex)
{
    assert(depIndex < this->depCount);
    assert(!this->allDepsAreNormal);

    // Dependent kinds are after the dependent loaders
    uint8_t* firstDepKind = (uint8_t*)&dependents[this->depCount];
    return ((JustInTimeLoader::DependentKind*)firstDepKind)[depIndex];
}

Loader* JustInTimeLoader::dependent(const RuntimeState& state, uint32_t depIndex, DependentKind* kind) const
{
    assert(depIndex < this->depCount);
    if ( kind != nullptr ) {
        if ( this->allDepsAreNormal )
            *kind = DependentKind::normal;
        else
            *kind = ((JustInTimeLoader*)this)->dependentKind(depIndex);
    }

    return dependents[depIndex];
}

bool JustInTimeLoader::getExportsTrie(uint64_t& runtimeOffset, uint32_t& size) const
{
    if ( this->exportsTrieRuntimeOffset != 0 ) {
        runtimeOffset = this->exportsTrieRuntimeOffset;
        size          = this->exportsTrieSize;
        return true;
    }
    return false;
}

bool JustInTimeLoader::hiddenFromFlat(bool forceGlobal) const
{
    if ( forceGlobal )
        this->hidden = false;
    return this->hidden;
}

bool JustInTimeLoader::representsCachedDylibIndex(uint16_t dylibIndex) const
{
    // check if this is an override of the specified cached dylib
    if ( this->overridesCache && (this->overrideIndex == dylibIndex) )
        return true;

    // check if this is the specified dylib in the cache
    if ( this->dylibInDyldCache && (this->ref.index == dylibIndex) )
        return true;

    return false;
}

void JustInTimeLoader::logFixup(RuntimeState& state, uint64_t fixupLocRuntimeOffset, uintptr_t newValue, PointerMetaData pmd, const Loader::ResolvedSymbol& target) const
{
    const MachOAnalyzer* ma       = this->analyzer();
    uintptr_t*           fixupLoc = (uintptr_t*)((uint8_t*)ma + fixupLocRuntimeOffset);
    switch ( target.kind ) {
        case Loader::ResolvedSymbol::Kind::rebase:
#if BUILDING_DYLD && __has_feature(ptrauth_calls)
            if ( pmd.authenticated )
                state.log("rebase: *0x%012lX = 0x%012lX (*%s+0x%012lX = 0x%012lX+0x%012lX) (JOP: diversity=0x%04X, addr-div=%d, key=%s)\n",
                          (long)fixupLoc, newValue,
                          this->leafName(), (long)fixupLocRuntimeOffset,
                          (uintptr_t)ma, (long)target.targetRuntimeOffset,
                          pmd.diversity, pmd.usesAddrDiversity, MachOLoaded::ChainedFixupPointerOnDisk::Arm64e::keyName(pmd.key));
            else
#endif
                state.log("rebase: *0x%012lX = 0x%012lX (*%s+0x%012lX = 0x%012lX+0x%012lX)\n",
                          (long)fixupLoc, newValue,
                          this->leafName(), (long)fixupLocRuntimeOffset,
                          (uintptr_t)ma, (long)target.targetRuntimeOffset);
            break;
        case Loader::ResolvedSymbol::Kind::bindToImage:
#if BUILDING_DYLD && __has_feature(ptrauth_calls)
            if ( pmd.authenticated )
                state.log("bind:   *0x%012lX = 0x%012lX (*%s+0x%012lX = %s/%s) (JOP: diversity=0x%04X, addr-div=%d, key=%s)\n",
                          (long)fixupLoc, newValue,
                          this->leafName(), (long)fixupLocRuntimeOffset,
                          target.targetLoader->leafName(), target.targetSymbolName,
                          pmd.diversity, pmd.usesAddrDiversity, MachOLoaded::ChainedFixupPointerOnDisk::Arm64e::keyName(pmd.key));
            else
#endif
                state.log("bind:   *0x%012lX = 0x%012lX (*%s+0x%012lX = %s/%s)\n",
                          (long)fixupLoc, newValue,
                          this->leafName(), (long)fixupLocRuntimeOffset,
                          target.targetLoader->leafName(), target.targetSymbolName);
            break;
        case Loader::ResolvedSymbol::Kind::bindAbsolute:
            state.log("bind:   *0x%012lX = 0x%012lX (*%s+0x%012lX = 0x%012lX(%s))\n",
                      (long)fixupLoc, newValue,
                      this->leafName(), (long)fixupLocRuntimeOffset,
                      (long)target.targetRuntimeOffset, target.targetSymbolName);
            break;
    }
}

bool JustInTimeLoader::overridesDylibInCache(const DylibPatch*& patchTable, uint16_t& cacheDylibOverriddenIndex) const
{
    if ( !this->overridesCache )
        return false;

    patchTable                = this->overridePatches;
    cacheDylibOverriddenIndex = this->overrideIndex;
    return true;
}


void JustInTimeLoader::handleStrongWeakDefOverrides(RuntimeState& state, DyldCacheDataConstLazyScopedWriter& cacheDataConst)
{
    CacheWeakDefOverride cacheWeakDefFixup = ^(uint32_t cachedDylibIndex, uint32_t cachedDylibVMOffset, const ResolvedSymbol& target) {
        JustInTimeLoader::cacheWeakDefFixup(state, cacheDataConst, cachedDylibIndex, cachedDylibVMOffset, target);
    };

    // Find an on-disk dylib with weak-defs, if one exists.  If we find one, look for strong overrides of all the special weak symbols
    // On all platforms we look in the main executable for strong symbols
    const Loader* weakDefLoader = nullptr;
    if ( state.mainExecutableLoader->analyzer(state)->hasWeakDefs() )
        weakDefLoader = state.mainExecutableLoader;

    // On macOS, we also allow check on-disk dylibs for strong symbols
#if TARGET_OS_OSX
    if ( weakDefLoader == nullptr ) {
        for (const Loader* loader : state.loaded) {
            if ( !loader->dylibInDyldCache ) {
                const dyld3::MachOAnalyzer *ma = loader->analyzer(state);
                if ( ma->hasWeakDefs() && ma->hasOpcodeFixups() ) {
                    weakDefLoader = loader;
                    break;
                }
            }
        }
    }
#endif

    if ( weakDefLoader != nullptr ) {
        MachOAnalyzer::forEachTreatAsWeakDef(^(const char* symbolName) {
            Diagnostics weakBindDiag; // ignore failures here
            (void)weakDefLoader->resolveSymbol(weakBindDiag, state, BIND_SPECIAL_DYLIB_WEAK_LOOKUP, symbolName, true, false, cacheWeakDefFixup);
        });
    }
}

void JustInTimeLoader::cacheWeakDefFixup(RuntimeState& state, DyldCacheDataConstLazyScopedWriter& cacheDataConst,
                                         uint32_t cachedDylibIndex, uint32_t cachedDylibVMOffset, const ResolvedSymbol& target) {
    const DyldSharedCache* dyldcache = state.config.dyldCache.addr;

    //state.log("cache patch: dylibIndex=%d, exportCacheOffset=0x%08X, target=%s\n", cachedDylibIndex, exportCacheOffset,target.targetSymbolName);
    dyldcache->forEachPatchableUseOfExport(cachedDylibIndex, cachedDylibVMOffset,
                                           ^(uint64_t cacheVMOffset,
                                             dyld3::MachOLoaded::PointerMetaData pmd, uint64_t addend) {
        uintptr_t* loc     = (uintptr_t*)(((uint8_t*)dyldcache) + cacheVMOffset);
        uintptr_t  newImpl = (uintptr_t)(Loader::resolvedAddress(state, target) + addend);
#if __has_feature(ptrauth_calls)
        if ( pmd.authenticated )
            newImpl = MachOLoaded::ChainedFixupPointerOnDisk::Arm64e::signPointer(newImpl, loc, pmd.usesAddrDiversity, pmd.diversity, pmd.key);
#endif
        // ignore duplicate patch entries
        if ( *loc != newImpl ) {
            cacheDataConst.makeWriteable();
            if ( state.config.log.fixups )
                state.log("cache patch: %p = 0x%0lX\n", loc, newImpl);
            *loc = newImpl;
        }
    });
}

void JustInTimeLoader::applyFixups(Diagnostics& diag, RuntimeState& state, DyldCacheDataConstLazyScopedWriter& cacheDataConst, bool allowLazyBinds) const
{
    //state.log("applyFixups: %s\n", this->path());
#if BUILDING_DYLD
    // if this is in the dyld cache there is normally no fixups need
    if ( this->dylibInDyldCache ) {
        // But if some lower level cached dylib has a root, we
        // need to patch this image's uses of that rooted dylib.
        if ( state.hasOverriddenCachedDylib() ) {
            // have each other image apply to me any cache patching it has
            for ( const Loader* ldr : state.loaded ) {
                ldr->applyCachePatchesTo(state, this, cacheDataConst);
            }
        }

        // images in shared cache don't need any more fixups
        return;
    }
#endif

    CacheWeakDefOverride cacheWeakDefFixup = ^(uint32_t cachedDylibIndex, uint32_t cachedDylibVMOffset, const ResolvedSymbol& target) {
        JustInTimeLoader::cacheWeakDefFixup(state, cacheDataConst, cachedDylibIndex, cachedDylibVMOffset, target);
    };

    // build targets table
    STACK_ALLOC_OVERFLOW_SAFE_ARRAY(const void*, bindTargets, 512);
    STACK_ALLOC_OVERFLOW_SAFE_ARRAY(const void*, overrideTargetAddrs, 32);
    STACK_ALLOC_OVERFLOW_SAFE_ARRAY(MissingFlatLazySymbol, missingFlatLazySymbols, 4);
    this->forEachBindTarget(diag, state, cacheWeakDefFixup, allowLazyBinds, ^(const ResolvedSymbol& target, bool& stop) {
        const void* targetAddr = (const void*)Loader::interpose(state, Loader::resolvedAddress(state, target), this);
        if ( state.config.log.fixups ) {
            const char* targetLoaderName = target.targetLoader ? target.targetLoader->leafName() : "<none>";
            state.log("<%s/bind#%lu> -> %p (%s/%s)\n", this->leafName(), bindTargets.count(), targetAddr, targetLoaderName, target.targetSymbolName);
        }

        // Record missing flat-namespace lazy symbols
        if ( targetAddr == state.libdyldMissingSymbol )
            missingFlatLazySymbols.push_back({ target.targetSymbolName, (uint32_t)bindTargets.count() });
        bindTargets.push_back(targetAddr);
    }, ^(const ResolvedSymbol& target, bool& stop) {
        // Missing weak binds need placeholders to make the target indices line up, but we should otherwise ignore them
        if ( (target.kind == Loader::ResolvedSymbol::Kind::bindToImage) && (target.targetLoader == nullptr) ) {
            if ( state.config.log.fixups )
                state.log("<%s/bind#%lu> -> missing-weak-bind (%s)\n", this->leafName(), overrideTargetAddrs.count(), target.targetSymbolName);

            overrideTargetAddrs.push_back((const void*)UINTPTR_MAX);
        } else {
            const void* targetAddr = (const void*)Loader::interpose(state, Loader::resolvedAddress(state, target), this);
            if ( state.config.log.fixups ) {
                const char* targetLoaderName = target.targetLoader ? target.targetLoader->leafName() : "<none>";
                state.log("<%s/bind#%lu> -> %p (%s/%s)\n", this->leafName(), overrideTargetAddrs.count(), targetAddr, targetLoaderName, target.targetSymbolName);
            }

            // Record missing flat-namespace lazy symbols
            if ( targetAddr == state.libdyldMissingSymbol )
                missingFlatLazySymbols.push_back({ target.targetSymbolName, (uint32_t)overrideTargetAddrs.count() });
            overrideTargetAddrs.push_back(targetAddr);
        }
    });
    if ( diag.hasError() )
        return;

    // do fixups using bind targets table
    this->applyFixupsGeneric(diag, state, bindTargets, overrideTargetAddrs, true, missingFlatLazySymbols);

    // some old macOS games need __dyld section set up
    if ( (state.config.process.platform == dyld3::Platform::macOS) && (state.libdyldLoader != nullptr) ) {
        const MachOAnalyzer* ma = this->analyzer();
        if ( !ma->inDyldCache() ) {
            ma->forEachSupportedPlatform(^(dyld3::Platform platform, uint32_t minOS, uint32_t sdk) {
                if ( (platform == dyld3::Platform::macOS) && (minOS <= 0x000A0600) ) {
                    struct DATAdyld { void* dyldLazyBinder; dyld3::DyldLookFunc dyldFuncLookup; };
                    uint64_t  sectSize;
                    if ( DATAdyld* dyldSect = (DATAdyld*)ma->findSectionContent("__DATA", "__dyld", sectSize) ) {
                        //state.log("found __dyld section in %s\n", this->path());
                        uint64_t           dyld4SectSize;
                        const MachOLoaded* libdyldML = state.libdyldLoader->loadAddress(state);
                        if ( LibdyldDyld4Section* libdyld4Section = (LibdyldDyld4Section*)libdyldML->findSectionContent("__DATA", "__dyld4", dyld4SectSize, true) ) {
                            dyldSect->dyldLazyBinder = nullptr;
                            dyldSect->dyldFuncLookup = libdyld4Section->dyldLookupFuncAddr;
                        }
                    }
                }
            });
        }
    }

    // mark any __DATA_CONST segments read-only
    if ( this->hasReadOnlyData )
        this->makeSegmentsReadOnly(state);

    if ( diag.noError() )
        this->fixUpsApplied = true;
}

void JustInTimeLoader::unmap(RuntimeState& state, bool force) const
{
    if ( this->dylibInDyldCache )
        return;
    if ( !force && this->neverUnload )
        state.log("trying to unmap %s\n", this->path());
    assert(force || !this->neverUnload);
    size_t vmSize  = (size_t)this->analyzer()->mappedSize();
    void*  vmStart = (void*)(this->loadAddress(state));
    state.config.syscall.munmap(vmStart, vmSize);
    if ( state.config.log.segments )
        state.log("unmapped 0x%012lX->0x%012lX for %s\n", (long)vmStart, (long)vmStart + (long)vmSize, this->path());
}

bool JustInTimeLoader::hasBeenFixedUp(RuntimeState&) const
{
    // FIXME: We don't have a "fixed up" state, but if we have even started initialization then
    // we must be at least fixed up
    return inited;
}

bool JustInTimeLoader::beginInitializers(RuntimeState&)
{
    // do nothing if already initializers already run
    if ( inited )
        return true;

    // switch to being-inited state
    inited = true;
    return false;
}

void JustInTimeLoader::runInitializers(RuntimeState& state) const
{
    this->findAndRunAllInitializers(state);
}

////////////////////////  other functions /////////////////////////////////

static bool hasPlusLoad(const MachOAnalyzer* ma)
{
    Diagnostics diag;
    return ma->hasPlusLoadMethod(diag);
}

static bool hasDataConst(const MachOAnalyzer* ma)
{
    __block bool result = false;
    ma->forEachSegment(^(const MachOAnalyzer::SegmentInfo& info, bool& stop) {
        if ( info.readOnlyData )
            result = true;
    });
    return result;
}

JustInTimeLoader::JustInTimeLoader(const MachOAnalyzer* ma, const Loader::InitialOptions& options,
                                   const FileID& fileID)
    : Loader(options), fileIdent(fileID)
{
}

JustInTimeLoader* JustInTimeLoader::make(RuntimeState& state, const MachOAnalyzer* ma, const char* path, const FileID& fileID, uint64_t sliceOffset,
                                         bool willNeverUnload, bool leaveMapped, bool overridesCache, uint16_t overridesDylibIndex)
{
    //state.log("JustInTimeLoader::make(%s) willNeverUnload=%d\n", path, willNeverUnload);
    // use malloc and placement new to create object big enough for all info
    bool                   allDepsAreNormal     = true;
    uint32_t               depCount             = ma->dependentDylibCount(&allDepsAreNormal);
    uint32_t               minDepCount          = (depCount ? depCount - 1 : 1);
    size_t                 sizeNeeded           = sizeof(JustInTimeLoader) + (minDepCount * sizeof(AuthLoader)) + (allDepsAreNormal ? 0 : depCount) + strlen(path) + 1;
    void*                  storage              = state.longTermAllocator.malloc(sizeNeeded);
    Loader::InitialOptions options;
    options.inDyldCache     = DyldSharedCache::inDyldCache(state.config.dyldCache.addr, ma);
    options.hasObjc         = ma->hasObjC();
    options.mayHavePlusLoad = hasPlusLoad(ma);
    options.roData          = hasDataConst(ma);
    options.neverUnloaded   = willNeverUnload || overridesCache; // dylibs in cache never unload, be consistent and don't unload roots either
    options.leaveMapped     = leaveMapped;
    JustInTimeLoader* p     = new (storage) JustInTimeLoader(ma, options, fileID);

    // fill in extra data
    p->mappedAddress        = ma;
    p->pathOffset           = sizeof(JustInTimeLoader) + (minDepCount * sizeof(AuthLoader)) + (allDepsAreNormal ? 0 : depCount);
    p->dependentsSet        = false;
    p->fixUpsApplied        = false;
    p->inited               = false;
    p->hidden               = false;
    p->altInstallName       = ma->isDylib() && (strcmp(ma->installName(), path) != 0);
    p->lateLeaveMapped      = false;
    p->allDepsAreNormal     = allDepsAreNormal;
    p->padding              = 0;
    p->sliceOffset          = sliceOffset;

    if ( !ma->hasExportTrie(p->exportsTrieRuntimeOffset, p->exportsTrieSize) ) {
        p->exportsTrieRuntimeOffset = 0;
        p->exportsTrieSize          = 0;
    }
    p->overridePatches = nullptr;
    p->overridesCache  = overridesCache;
    p->overrideIndex   = overridesDylibIndex;
    p->depCount        = depCount;
    for ( unsigned i = 0; i < depCount; ++i ) {
        new (&p->dependents[i]) (AuthLoader) { nullptr };
        if ( !allDepsAreNormal )
            p->dependentKind(i) = DependentKind::normal;
    }
    strcpy(((char*)p) + p->pathOffset, path);
    //state.log("JustInTimeLoader::make(%p, %s) => %p\n", ma, path, p);

    state.add(p);
#if BUILDING_DYLD
    if ( overridesCache )
        state.setHasOverriddenCachedDylib();
    if ( state.config.log.loaders )
        state.log("using JustInTimeLoader %p for %s\n", p, path);
#endif

    return p;
}

// Used to build prebound targets in PrebuiltLoader.
void JustInTimeLoader::forEachBindTarget(Diagnostics& diag, RuntimeState& state, CacheWeakDefOverride cacheWeakDefFixup, bool allowLazyBinds,
                                         void (^callback)(const ResolvedSymbol& target, bool& stop),
                                         void (^overrideBindCallback)(const ResolvedSymbol& target, bool& stop)) const
{
    const MachOAnalyzer* ma          = (const MachOAnalyzer*)this->mappedAddress;
    __block unsigned     targetIndex = 0;
    __block unsigned     overrideBindTargetIndex = 0;
    ma->forEachBindTarget(diag, allowLazyBinds, ^(const MachOAnalyzer::BindTargetInfo& info, bool& stop) {
        // Regular and lazy binds
        assert(targetIndex == info.targetIndex);
        ResolvedSymbol targetInfo = this->resolveSymbol(diag, state, info.libOrdinal, info.symbolName, info.weakImport, info.lazyBind, cacheWeakDefFixup);
        targetInfo.targetRuntimeOffset += info.addend;
        callback(targetInfo, stop);
        if ( diag.hasError() )
            stop = true;
        ++targetIndex;
    }, ^(const MachOAnalyzer::BindTargetInfo& info, bool& stop) {
        // Opcode based weak binds
        assert(overrideBindTargetIndex == info.targetIndex);
        Diagnostics weakBindDiag; // failures aren't fatal here
        ResolvedSymbol targetInfo = this->resolveSymbol(weakBindDiag, state, info.libOrdinal, info.symbolName, info.weakImport, info.lazyBind, cacheWeakDefFixup);
        if ( weakBindDiag.hasError() ) {
            // In dyld2, it was also ok for a weak bind to be missing.  Then we would let the bind/rebase on this
            // address handle it
            targetInfo.targetLoader        = nullptr;
            targetInfo.targetRuntimeOffset = 0;
            targetInfo.kind                = ResolvedSymbol::Kind::bindToImage;
            targetInfo.isCode              = false;
            targetInfo.isWeakDef           = false;
        } else {
            targetInfo.targetRuntimeOffset += info.addend;
        }
        overrideBindCallback(targetInfo, stop);
        ++overrideBindTargetIndex;
    });
}

Loader::FileValidationInfo JustInTimeLoader::getFileValidationInfo() const
{
    __block FileValidationInfo result;
    // set checkInodeMtime and checkCdHash to false by default
    bzero(&result, sizeof(FileValidationInfo));
    if ( this->fileIdent.valid() ) {
        result.checkInodeMtime = true;
        result.sliceOffset     = this->sliceOffset;
        result.inode           = this->fileIdent.inode();
        result.mtime           = this->fileIdent.mtime();
    }
    if ( !this->dylibInDyldCache ) {
        const MachOAnalyzer* ma = this->analyzer();
        ma->forEachCDHash(^(const uint8_t aCdHash[20]) {
            result.checkCdHash = true;
            memcpy(&result.cdHash[0], &aCdHash[0], 20);
        });
    }
    return result;
}

const Loader::DylibPatch* JustInTimeLoader::getCatalystMacTwinPatches() const
{
    return this->overridePatchesCatalystMacTwin;
}

void JustInTimeLoader::withRegions(const MachOAnalyzer* ma, void (^callback)(const Array<Region>& regions))
{
    uint32_t segCount   = ma->segmentCount();
    uint64_t vmTextAddr = ma->preferredLoadAddress();
    STACK_ALLOC_ARRAY(Region, regions, segCount * 2);
    ma->forEachSegment(^(const MachOAnalyzer::SegmentInfo& segInfo, bool& stop) {
        Region region;
        if ( !segInfo.hasZeroFill || (segInfo.fileSize != 0) ) {
            // add region for content that is not wholely zerofill
            region.vmOffset     = segInfo.vmAddr - vmTextAddr;
            region.perms        = segInfo.protections;
            region.readOnlyData = segInfo.readOnlyData;
            region.isZeroFill   = false;
            region.fileOffset   = (uint32_t)segInfo.fileOffset;
            region.fileSize     = (uint32_t)segInfo.fileSize;
            // special case LINKEDIT, the vmsize is often larger than the filesize
            // but we need to mmap off end of file, otherwise we may have r/w pages at end
            if ( (segInfo.segIndex == segCount - 1) && (segInfo.protections == 1) ) {
                region.fileSize = (uint32_t)segInfo.vmSize;
            }
            regions.push_back(region);
        }
        if ( segInfo.hasZeroFill ) {
            Region fill;
            fill.vmOffset     = segInfo.vmAddr - vmTextAddr + segInfo.fileSize;
            fill.perms        = segInfo.protections;
            fill.readOnlyData = false;
            fill.isZeroFill   = true;
            fill.fileOffset   = 0;
            fill.fileSize     = (uint32_t)(segInfo.vmSize - segInfo.fileSize);
            regions.push_back(fill);
        }
    });
    callback(regions);
}

#if BUILDING_CACHE_BUILDER
JustInTimeLoader* JustInTimeLoader::makeJustInTimeLoaderDyldCache(RuntimeState& state, const MachOAnalyzer* ma, const char* installName,
                                                                  uint32_t dylibCacheIndex, const FileID& fileID, bool catalystTwin, uint32_t twinIndex)
{
    bool cacheOverride = catalystTwin;
    JustInTimeLoader* jitLoader = JustInTimeLoader::make(state, ma, installName, fileID, 0, true, false, cacheOverride, twinIndex);
    jitLoader->ref.app   = false;
    jitLoader->ref.index = dylibCacheIndex;
    return jitLoader;
}
#endif

#if BUILDING_UNIT_TESTS
JustInTimeLoader* JustInTimeLoader::makeJustInTimeLoader(RuntimeState& state, const MachOAnalyzer* ma, const char* installName)
{
    JustInTimeLoader* jitLoader = JustInTimeLoader::make(state, ma, installName, FileID::none(), 0, true, false, false, 0);
    return jitLoader;
}
#endif

Loader* JustInTimeLoader::makeJustInTimeLoaderDyldCache(Diagnostics& diag, RuntimeState& state, const char* loadPath, const LoadOptions& options, uint32_t dylibCacheIndex)
{
    const DyldSharedCache* cache = state.config.dyldCache.addr;
    uint64_t mtime = 0;
    uint64_t inode = 0;
    const MachOAnalyzer* cacheMA = (MachOAnalyzer*)cache->getIndexedImageEntry(dylibCacheIndex, mtime, inode);

    bool fileIDValid = cache->header.dylibsExpectedOnDisk;
    FileID fileID(inode, mtime, fileIDValid);
    if ( !cacheMA->loadableIntoProcess(state.config.process.platform, loadPath) ) {
        diag.error("wrong platform to load into process");
        return nullptr;
    }
    bool     catalystOverrideOfMacSide = false;
    uint32_t catalystOverideDylibIndex = 0;
    if ( strncmp(loadPath, "/System/iOSSupport/", 19) == 0 ) {
        uint32_t macIndex;
        if ( cache->hasImagePath(&loadPath[18], macIndex) ) {
            catalystOverrideOfMacSide = true;
            catalystOverideDylibIndex = macIndex;
        }
    }
    JustInTimeLoader* result = JustInTimeLoader::make(state, cacheMA, loadPath, fileID, 0, true, false, catalystOverrideOfMacSide, catalystOverideDylibIndex);
    result->ref.index = dylibCacheIndex;
#if BUILDING_DYLD
    if ( state.config.log.segments )
        result->logSegmentsFromSharedCache(state);
    if ( state.config.log.libraries )
        Loader::logLoad(state, cacheMA, loadPath);
#endif
    return result;
}

Loader* JustInTimeLoader::makeJustInTimeLoaderDisk(Diagnostics& diag, RuntimeState& state, const char* loadPath, const LoadOptions& options, bool overridesCache, uint32_t overridesCacheIndex)
{
    __block Loader* result          = nullptr;
    bool            checkIfOSBinary = state.config.process.archs->checksOSBinary();
    state.config.syscall.withReadOnlyMappedFile(diag, loadPath, checkIfOSBinary, ^(const void* mapping, size_t mappedSize, bool isOSBinary, const FileID& fileID, const char* canonicalPath) {
        if ( const MachOFile* mf = MachOFile::compatibleSlice(diag, mapping, mappedSize, loadPath, state.config.process.platform, isOSBinary, *state.config.process.archs) ) {
            // verify the filetype is loadable in this context
            if ( mf->isDylib() ) {
                if ( !options.canBeDylib ) {
                    diag.error("cannot load dylib '%s'", loadPath);
                    return;
                }
            }
            else if ( mf->isBundle() ) {
                if ( !options.canBeBundle ) {
                    diag.error("cannot link against bundle '%s'", loadPath);
                    return;
                }
            }
            else if ( mf->isMainExecutable() ) {
                if ( !options.canBeExecutable ) {
                    if ( options.staticLinkage )
                        diag.error("cannot link against a main executable '%s'", loadPath);
                    else
                        diag.error("cannot dlopen a main executable '%s'", loadPath);
                    return;
                }
            }
            else {
                diag.error("unloadable mach-o file type %d '%s'", mf->filetype, loadPath);
                return;
            }
            const MachOAnalyzer* ma          = (MachOAnalyzer*)mf;
            // FIXME: Enable the call to validMachOForArchAndPlatform below
            /*if ( !ma->validMachOForArchAndPlatform(diag, mappedSize, loadPath, *state.config.process.archs, state.config.process.platform, isOSBinary) ) {
                return;
            }*/
            bool                 leaveMapped = options.rtldNoDelete;
            bool                 neverUnload = !options.forceUnloadable && (options.launching || ma->neverUnload());
            uint64_t             vmSpace     = ma->mappedSize();
            CodeSignatureInFile  codeSignature;
            FileValidationInfo   fileValidation;
            bool                 hasCodeSignature = ma->hasCodeSignature(codeSignature.fileOffset, codeSignature.size);
            fileValidation.checkInodeMtime = fileID.valid();
            if ( fileValidation.checkInodeMtime ) {
                fileValidation.inode = fileID.inode();
                fileValidation.mtime = fileID.mtime();
            }
            fileValidation.sliceOffset     = (uint8_t*)mf - (uint8_t*)mapping;
            JustInTimeLoader::withRegions(ma, ^(const Array<Region>& regions) {
#if BUILDING_CACHE_BUILDER
                // in cache builder, files are already mapped
                (void)vmSpace;
                result = JustInTimeLoader::make(state, ma, canonicalPath, FileID::none(), fileValidation.sliceOffset, neverUnload, leaveMapped, overridesCache, overridesCacheIndex);
#else
                if ( const MachOAnalyzer* realMA = Loader::mapSegments(diag, state, canonicalPath, vmSpace, codeSignature, hasCodeSignature, regions, neverUnload, false, fileValidation) ) {
                    result = JustInTimeLoader::make(state, realMA, canonicalPath, fileID, fileValidation.sliceOffset, neverUnload, leaveMapped, overridesCache, overridesCacheIndex);
                    if ( options.rtldLocal )
                        ((JustInTimeLoader*)result)->hidden = true;
                }
#endif
            });
        }
    });
    return result;
}

Loader* JustInTimeLoader::makeLaunchLoader(Diagnostics& diag, RuntimeState& state, const MachOAnalyzer* mainExe, const char* mainExePath)
{
    FileID   mainFileID = FileID::none();
    uint64_t mainSliceOffset = 0; // FIXME
#if !BUILDING_CACHE_BUILDER
    state.config.syscall.fileExists(mainExePath, &mainFileID);
#endif
    return JustInTimeLoader::make(state, mainExe, mainExePath, mainFileID, mainSliceOffset, true, false, false, 0);
}

} // namespace
