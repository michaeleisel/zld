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

#ifndef JustInTimeLoader_h
#define JustInTimeLoader_h


#include <stdint.h>
#include <unistd.h>

#include "Loader.h"
#include "Array.h"
#include "MachOAnalyzer.h"

class DyldSharedCache;

namespace dyld4 {

using dyld3::MachOAnalyzer;

class PrebuiltLoader;
class DyldCacheDataConstLazyScopedWriter;

class JustInTimeLoader : public Loader
{
public:
    // these are the "virtual" methods that override Loader
    const MachOLoaded*  loadAddress(RuntimeState&) const;
    const char*         path() const;
    bool                contains(RuntimeState& state, const void* addr, const void** segAddr, uint64_t* segSize, uint8_t* segPerms) const;
    bool                matchesPath(const char* path) const;
    FileID              fileID() const;
    uint32_t            dependentCount() const;
    Loader*             dependent(const RuntimeState& state, uint32_t depIndex, DependentKind* kind=nullptr) const;
    bool                getExportsTrie(uint64_t& runtimeOffset, uint32_t& size) const;
    bool                hiddenFromFlat(bool forceGlobal) const;
    bool                representsCachedDylibIndex(uint16_t dylibIndex) const;
    void                loadDependents(Diagnostics& diag, RuntimeState& state, const LoadOptions& options);
    void                unmap(RuntimeState& state, bool force=false) const;
    void                applyFixups(Diagnostics&, RuntimeState& state, DyldCacheDataConstLazyScopedWriter&, bool allowLazyBinds) const;
    bool                overridesDylibInCache(const DylibPatch*& patchTable, uint16_t& cacheDylibOverriddenIndex) const;
    // these are private "virtual" methods
    bool                hasBeenFixedUp(RuntimeState&) const;
    bool                beginInitializers(RuntimeState&);
    void                runInitializers(RuntimeState&) const;

    bool                shouldLeaveMapped() const { return this->leaveMapped || this->lateLeaveMapped; }
    void                setLateLeaveMapped() { this->lateLeaveMapped = true; }
    bool                isOverrideOfCachedDylib() const { return overridesCache; }

    // functions are used to create PrebuiltLoader from JustInTimeLoader
    void                forEachBindTarget(Diagnostics& diag, RuntimeState& state, CacheWeakDefOverride patcher, bool allowLazyBinds,
                                          void (^callback)(const ResolvedSymbol& target, bool& stop),
                                          void (^overrideBindCallback)(const ResolvedSymbol& target, bool& stop)) const;
    FileValidationInfo  getFileValidationInfo() const;
    static void         withRegions(const MachOAnalyzer* ma, void (^callback)(const Array<Region>& regions));

    static void         handleStrongWeakDefOverrides(RuntimeState& state, DyldCacheDataConstLazyScopedWriter& cacheDataConst);

    // Wehn patching an iOSMac dylib, we may need an additional patch table for the macOS twin. This returns that patch table
    const DylibPatch*   getCatalystMacTwinPatches() const;


#if BUILDING_CACHE_BUILDER
    static JustInTimeLoader*    makeJustInTimeLoaderDyldCache(RuntimeState& state, const MachOAnalyzer* ma, const char* loadPath, uint32_t dylibCacheIndex, const FileID& fileID, bool twin, uint32_t twinIndex);
#endif
#if BUILDING_UNIT_TESTS
    static JustInTimeLoader*    makeJustInTimeLoader(RuntimeState& state, const MachOAnalyzer* ma, const char* loadPath);
#endif
    static Loader*      makeJustInTimeLoaderDyldCache(Diagnostics& diag, RuntimeState& state, const char* loadPath, const LoadOptions& options, uint32_t dylibCacheIndex);
    static Loader*      makeJustInTimeLoaderDisk(Diagnostics& diag, RuntimeState& state, const char* loadPath, const LoadOptions& options, bool overridesCache=false, uint32_t overridesCacheIndex=0);
    static Loader*      makeLaunchLoader(Diagnostics& diag, RuntimeState& state, const MachOAnalyzer* mainExe, const char* mainExePath);

private:

    const MachOLoaded*   mappedAddress;
    mutable uint64_t     pathOffset         : 16,
                         dependentsSet      :  1,
                         fixUpsApplied      :  1,
                         inited             :  1,
                         hidden             :  1,
                         altInstallName     :  1,
                         lateLeaveMapped    :  1,
                         overridesCache     :  1,
                         allDepsAreNormal   :  1,
                         overrideIndex      : 15,
                         depCount           : 16,
                         padding            :  9;
    uint64_t             sliceOffset;
    FileID               fileIdent;
    const DylibPatch*    overridePatches;
    const DylibPatch*    overridePatchesCatalystMacTwin;
    uint32_t             exportsTrieRuntimeOffset;
    uint32_t             exportsTrieSize;
    AuthLoader           dependents[1];
    // DependentsKind[]: If allDepsAreNormal is false, then we have an array here too, with 1 entry per dependent

    const MachOAnalyzer*        analyzer() const { return (MachOAnalyzer*)mappedAddress; }
    void                        logFixup(RuntimeState& state, uint64_t fixupLocRuntimeOffset, uintptr_t newValue, PointerMetaData pmd, const Loader::ResolvedSymbol& target) const;
    void                        fixupUsesOfOverriddenDylib(const JustInTimeLoader* overriddenDylib) const;
    DependentKind&              dependentKind(uint32_t depIndex);
    static void                 cacheWeakDefFixup(RuntimeState& state, DyldCacheDataConstLazyScopedWriter& cacheDataConst,
                                                  uint32_t cachedDylibIndex, uint32_t cachedDylibVMOffset, const ResolvedSymbol& target);
    const DylibPatch*           makePatchTable(RuntimeState& state, uint32_t indexOfOverriddenCachedDylib) const;
    static JustInTimeLoader*    make(RuntimeState& state, const MachOAnalyzer* ma, const char* path, const FileID& fileID, uint64_t sliceOffset,
                                     bool willNeverUnload, bool leaveMapped, bool overridesCache, uint16_t dylibIndex);



private:
                        JustInTimeLoader(const MachOAnalyzer* ma, const Loader::InitialOptions& options, const FileID& fileID);
};


}  // namespace dyld4

#endif // JustInTimeLoader_h





