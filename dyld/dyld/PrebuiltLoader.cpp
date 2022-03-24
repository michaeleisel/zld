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
#include <sys/fcntl.h>

#include "Loader.h"
#include "PrebuiltLoader.h"
#include "JustInTimeLoader.h"
#include "BumpAllocator.h"
#include "MachOAnalyzer.h"
#include "DyldProcessConfig.h"
#include "DyldRuntimeState.h"
#include "PrebuiltLoader_version.h"
#include "PrebuiltObjC.h"
#include "OptimizerObjC.h"
#include "objc-shared-cache.h"

#define DYLD_CLOSURE_XATTR_NAME "com.apple.dyld"

using dyld3::MachOAnalyzer;
using dyld3::OverflowSafeArray;

namespace dyld4 {

class PrebuiltLoader;

//
// MARK: --- PrebuiltLoader::BindTargetRef methods ---
//

PrebuiltLoader::BindTargetRef::BindTargetRef(const ResolvedSymbol& targetSymbol)
{
    uint64_t value63;
    uint64_t high2;
    uint64_t high8;
    uint64_t low39;
    switch ( targetSymbol.kind ) {
        case ResolvedSymbol::Kind::bindAbsolute:
            value63    = targetSymbol.targetRuntimeOffset & 0x7FFFFFFFFFFFFFFFULL;
            high2      = targetSymbol.targetRuntimeOffset >> 62;
            _abs.kind  = 1;
            _abs.value = value63;
            assert((high2 == 0) || (high2 == 3) && "unencodeable absolute symbol value");
            break;
        case ResolvedSymbol::Kind::bindToImage: {
            LoaderRef loaderRef = (targetSymbol.targetLoader != nullptr) ? targetSymbol.targetLoader->ref : LoaderRef::missingWeakImage();

            high8              = targetSymbol.targetRuntimeOffset >> 56;
            low39              = targetSymbol.targetRuntimeOffset & 0x7FFFFFFFFFULL;
            _regular.kind      = 0;
            _regular.loaderRef = *(uint16_t*)(&loaderRef);
            _regular.high8     = high8;
            _regular.low39     = low39;
            assert((offset() == targetSymbol.targetRuntimeOffset) && "large offset not support");
            break;
        }
        case ResolvedSymbol::Kind::rebase:
            assert("rebase not a valid bind target");
            break;
    }
}

uint64_t PrebuiltLoader::BindTargetRef::value(RuntimeState& state) const
{
    if ( _abs.kind ) {
        uint64_t value = _abs.value;
        // sign extend
        if ( value & 0x4000000000000000ULL )
            value |= 0x8000000000000000ULL;
        return value;
    }
    else {
        return (uint64_t)(this->loaderRef().loader(state)->loadAddress(state)) + this->offset();
    }
}

//
// MARK: --- PrebuiltLoader methods ---
//

PrebuiltLoader::LoaderRef PrebuiltLoader::BindTargetRef::loaderRef() const
{
    assert(_regular.kind == 0);
    uint16_t t = _regular.loaderRef;
    return *((LoaderRef*)&t);
}

uint64_t PrebuiltLoader::BindTargetRef::offset() const
{
    assert(_regular.kind == 0);
    uint64_t signedOffset = _regular.low39;
    if ( signedOffset & 0x0000004000000000ULL )
        signedOffset |= 0x00FFFF8000000000ULL;
    return ((uint64_t)_regular.high8 << 56) | signedOffset;
}

const char* PrebuiltLoader::BindTargetRef::loaderLeafName(RuntimeState& state) const
{
    if ( _abs.kind ) {
        return "<absolute>";
    }
    else {
        return this->loaderRef().loader(state)->leafName();
    }
}

PrebuiltLoader::BindTargetRef PrebuiltLoader::BindTargetRef::makeAbsolute(uint64_t value) {
    return PrebuiltLoader::BindTargetRef(value);
}

PrebuiltLoader::BindTargetRef::BindTargetRef(uint64_t absoluteValue) {
    uint64_t value63;
    uint64_t high2;
    value63     = absoluteValue & 0x7FFFFFFFFFFFFFFFULL;
    high2       = absoluteValue >> 62;
    _abs.kind   = 1;
    _abs.value  = value63;
    assert((high2 == 0) || (high2 == 3) && "unencodeable absolute symbol value");
}

PrebuiltLoader::BindTargetRef::BindTargetRef(const BindTarget& bindTarget) {
    LoaderRef loaderRef = (bindTarget.loader != nullptr) ? bindTarget.loader->ref : LoaderRef::missingWeakImage();
    uint64_t high8;
    uint64_t low39;
    high8               = bindTarget.runtimeOffset >> 56;
    low39               = bindTarget.runtimeOffset & 0x7FFFFFFFFFULL;
    _regular.kind       = 0;
    _regular.loaderRef  = *(uint16_t*)(&loaderRef);
    _regular.high8      = high8;
    _regular.low39      = low39;
    assert((offset() == bindTarget.runtimeOffset) && "large offset not support");
}

////////////////////////   "virtual" functions /////////////////////////////////

const char* PrebuiltLoader::path() const
{
    return this->pathOffset ? ((char*)this + this->pathOffset) : nullptr;
}

const MachOLoaded* PrebuiltLoader::loadAddress(RuntimeState& state) const
{
    if ( this->ref.app )
        return state.appLoadAddress(this->ref.index);
    else
        return state.cachedDylibLoadAddress(this->ref.index);
}

bool PrebuiltLoader::contains(RuntimeState& state, const void* addr, const void** segAddr, uint64_t* segSize, uint8_t* segPerms) const
{
    const uint8_t* loadAddr = (uint8_t*)(this->loadAddress(state));
    if ( (uint8_t*)addr < loadAddr )
        return false;
    size_t targetOffset = (uint8_t*)addr - loadAddr;
    for ( const Region& seg : this->segments() ) {
        if ( (targetOffset >= seg.vmOffset) && (targetOffset < (seg.vmOffset + seg.fileSize)) ) {
            *segAddr  = (void*)(loadAddr + seg.vmOffset);
            *segSize  = seg.fileSize;
            *segPerms = seg.perms;
            return true;
        }
    }
    return false;
}

bool PrebuiltLoader::matchesPath(const char* path) const
{
    if ( strcmp(path, this->path()) == 0 )
        return true;
    if ( altPathOffset != 0 ) {
        const char* altPath = (char*)this + this->altPathOffset;
        if ( strcmp(path, altPath) == 0 )
            return true;
    }
    return false;
}

FileID PrebuiltLoader::fileID() const
{
    if ( const FileValidationInfo* fvi = fileValidationInfo() )
        return FileID(fvi->inode, fvi->mtime, fvi->checkInodeMtime);
    return FileID::none();
}

uint32_t PrebuiltLoader::dependentCount() const
{
    return this->depCount;
}

bool PrebuiltLoader::recordedCdHashIs(const uint8_t expectedCdHash[20]) const
{
    if ( const FileValidationInfo* fvi = fileValidationInfo() ) {
        if ( fvi->checkCdHash )
            return (::memcmp(fvi->cdHash, expectedCdHash, 20) == 0);
    }
    return false;
}

#if BUILDING_CACHE_BUILDER
void PrebuiltLoader::withCDHash(void (^callback)(const uint8_t cdHash[20])) const
{
    // FIXME: Should fileValidationInfo() check for a 0 offset instead?
    if ( this->fileValidationOffset == 0 )
        return;

    if ( const FileValidationInfo* fvi = fileValidationInfo() ) {
        if ( fvi->checkCdHash )
            callback(fvi->cdHash);
    }
}
#endif

void PrebuiltLoader::map(Diagnostics& diag, RuntimeState& state, const LoadOptions& options) const
{
    State& ldrState = this->loaderState(state);

    // only map once
    if ( ldrState >= State::mapped )
        return;

#if BUILDING_DYLD
    if ( this->overridesCache )
        state.setHasOverriddenCachedDylib();
    if ( state.config.log.loaders)
        state.log("using PrebuiltLoader %p for %s\n", this, this->path());
#endif

    if ( this->dylibInDyldCache ) {
        // dylibs in cache already mapped, just need to update its state
        ldrState = State::mapped;
#if BUILDING_DYLD
        if ( state.config.log.segments )
            this->logSegmentsFromSharedCache(state);
        if ( state.config.log.libraries )
            Loader::logLoad(state, this->loadAddress(state), this->path());
#endif
    }
    else if ( this == state.mainExecutableLoader ) {
        // main executable is mapped the the kernel, we need to jump ahead to that state
        if ( ldrState < State::mapped )
            ldrState = State::mapped;
        this->setLoadAddress(state, state.config.process.mainExecutable);
    }
    else {
        const MachOLoaded* ml = Loader::mapSegments(diag, state, this->path(), this->vmSpace, this->codeSignature, true,
                                                    this->segments(), this->neverUnload, true, *this->fileValidationInfo());
        if ( diag.hasError() )
            return;
        this->setLoadAddress(state, ml);
        ldrState = State::mapped;
    }

    // add to `state.loaded` but avoid duplicates with inserted dyld cache dylibs
    if ( state.config.pathOverrides.hasInsertedDylibs() ) {
        for (const Loader* ldr : state.loaded) {
            if ( ldr == this )
                return;
        }
    }
    state.add((Loader*)this);
}

void PrebuiltLoader::loadDependents(Diagnostics& diag, RuntimeState& state, const LoadOptions& options)
{
    State& ldrState = this->loaderState(state);

    // mmap() this image if needed
    this->map(diag, state, options);

    // break cycles
    if ( ldrState >= State::mappingDependents )
        return;

    // breadth-first map all dependents
    ldrState = State::mappingDependents;
    PrebuiltLoader* deps[this->depCount];
    for ( int depIndex = 0; depIndex < this->depCount; ++depIndex ) {
        PrebuiltLoader* child = (PrebuiltLoader*)dependent(state, depIndex);
        deps[depIndex]        = child;
        if ( child != nullptr )
            child->map(diag, state, options);
    }
    LoadChain   nextChain { options.rpathStack, this };
    LoadOptions depOptions = options;
    depOptions.rpathStack  = &nextChain;
    for ( int depIndex = 0; depIndex < this->depCount; ++depIndex ) {
        if ( deps[depIndex] != nullptr )
            deps[depIndex]->loadDependents(diag, state, depOptions);
    }
    ldrState = State::dependentsMapped;
}

void PrebuiltLoader::unmap(RuntimeState& state, bool force) const
{
    // only called during a dlopen() failure, roll back state 
    State& ldrState = this->loaderState(state);
    ldrState = State::notMapped;
}

void PrebuiltLoader::applyFixups(Diagnostics& diag, RuntimeState& state, DyldCacheDataConstLazyScopedWriter& cacheDataConst, bool allowLazyBinds) const
{
    //state.log("PrebuiltLoader::applyFixups: %s\n", this->path());

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
    }
#endif

    // no fixups for dylibs in dyld cache if the Loader is in the shared cache too
    State& ldrState = this->loaderState(state);
    if ( this->dylibInDyldCache && !this->ref.app ) {
        ldrState = PrebuiltLoader::State::fixedUp;
        return;
    }

    // build targets table
    STACK_ALLOC_OVERFLOW_SAFE_ARRAY(const void*, targetAddrs, 512);
    for ( const BindTargetRef& target : this->bindTargets() ) {
        void* value = (void*)(long)target.value(state);
        if ( state.config.log.fixups ) {
            if ( target.isAbsolute() )
                state.log("<%s/bind#%lu> -> %p\n", this->leafName(), targetAddrs.count(), value);
            else
                state.log("<%s/bind#%lu> -> %p (%s+0x%08llX)\n", this->leafName(), targetAddrs.count(), value, target.loaderRef().loader(state)->leafName(), target.offset());
        }
        targetAddrs.push_back(value);
    }
    if ( diag.hasError() )
        return;

    STACK_ALLOC_OVERFLOW_SAFE_ARRAY(const void*, overrideTargetAddrs, 32);
    for ( const BindTargetRef& target : this->overrideBindTargets() ) {
        // Missing weak binds need placeholders to make the target indices line up, but we should otherwise ignore them
        if ( !target.isAbsolute() && target.loaderRef().isMissingWeakImage() ) {
            if ( state.config.log.fixups )
                state.log("<%s/bind#%lu> -> missing-weak-bind\n", this->leafName(), overrideTargetAddrs.count());

            overrideTargetAddrs.push_back((const void*)UINTPTR_MAX);
        } else {
            void* value = (void*)(long)target.value(state);
            if ( state.config.log.fixups ) {
                if ( target.isAbsolute() )
                    state.log("<%s/bind#%lu> -> %p\n", this->leafName(), overrideTargetAddrs.count(), value);
                else
                    state.log("<%s/bind#%lu> -> %p (%s+0x%08llX)\n", this->leafName(), overrideTargetAddrs.count(), value, target.loaderRef().loader(state)->leafName(), target.offset());
            }
            overrideTargetAddrs.push_back(value);
        }
    }
    if ( diag.hasError() )
        return;

    // do fixups using bind targets table
    this->applyFixupsGeneric(diag, state, targetAddrs, overrideTargetAddrs, true, {});

    // ObjC may have its own fixups which override those we just applied
    applyObjCFixups(state);

    // mark any __DATA_CONST segments read-only
    if ( this->hasReadOnlyData )
        this->makeSegmentsReadOnly(state);

    // update state
    ldrState = PrebuiltLoader::State::fixedUp;
}

Loader* PrebuiltLoader::dependent(const RuntimeState& state, uint32_t depIndex, DependentKind* kind) const
{
    assert(depIndex < this->depCount);
    if ( kind != nullptr ) {
        if ( this->dependentKindArrayOffset != 0 ) {
            const DependentKind* kindsArray = (DependentKind*)((uint8_t*)this + this->dependentKindArrayOffset);
            *kind                           = kindsArray[depIndex];
        }
        else {
            *kind = DependentKind::normal;
        }
    }
    const PrebuiltLoader::LoaderRef* depRefsArray = (PrebuiltLoader::LoaderRef*)((uint8_t*)this + this->dependentLoaderRefsArrayOffset);
    PrebuiltLoader::LoaderRef        depLoaderRef = depRefsArray[depIndex];
    if ( depLoaderRef.isMissingWeakImage() )
        return nullptr;

    const PrebuiltLoader* depLoader = depLoaderRef.loader(state);
    // if we are in a catalyst app and this is a dylib in cache that links with something that does not support catalyst
    if ( this->dylibInDyldCache && !depLoader->supportsCatalyst && state.config.process.catalystRuntime ) {
        // switch to unzippered twin if there is one, if not, well, keep using macOS dylib...
        if ( depLoader->indexOfTwin != kNoUnzipperedTwin ) {
            PrebuiltLoader::LoaderRef twin(false, depLoader->indexOfTwin);
            depLoader = twin.loader(state);
        }
    }
    return (Loader*)depLoader;
}

bool PrebuiltLoader::getExportsTrie(uint64_t& runtimeOffset, uint32_t& size) const
{
    runtimeOffset = this->exportsTrieLoaderOffset;
    size          = this->exportsTrieLoaderSize;
    return (size != 0);
}

bool PrebuiltLoader::hiddenFromFlat(bool forceGlobal) const
{
    return false; // FIXME
}

bool PrebuiltLoader::representsCachedDylibIndex(uint16_t dylibIndex) const
{
    dylibIndex = 0xFFFF;
    return false; // cannot make PrebuiltLoader for images that override the dyld cache
}


void PrebuiltLoader::recursiveMarkBeingValidated(const RuntimeState& state) const
{
    State pbLdrState = this->loaderState(state);
    if ( pbLdrState == State::unknown ) {
        this->loaderState(state) = State::beingValidated;
        bool haveInvalidDependent = false;
        for (int depIndex = 0; depIndex < this->depCount; ++depIndex) {
            if ( const Loader* dep = this->dependent(state, depIndex) ) {
                assert (dep->isPrebuilt);
                const PrebuiltLoader* pbDep = (PrebuiltLoader*)dep;
                pbDep->recursiveMarkBeingValidated(state);
                if ( pbDep->loaderState(state) == State::invalid )
                    haveInvalidDependent = true;
            }
        }
        if ( haveInvalidDependent )
            this->loaderState(state) = State::invalid;
    }
}


// Note: because of cycles, isValid() cannot just call isValid() on each of its dependents
// Instead we do this in three steps:
// 1) recursively mark all reachable Loaders as beingValidated
// 2) check each beingValidated Loader for an override (which invalidates the PrebuiltLoader)
// 3) propagate up invalidness
bool PrebuiltLoader::isValid(const RuntimeState& state) const
{
    static const bool verbose = false;

    // quick exit if already known to be valid or invalid
    switch ( this->loaderState(state) ) {
        case State::unknown:
            // mark everything it references as beingValidated
            this->recursiveMarkBeingValidated(state);
            break;
        case State::beingValidated:
            break;
        case State::notMapped:
        case State::mapped:
        case State::mappingDependents:
        case State::dependentsMapped:
        case State::fixedUp:
        case State::beingInitialized:
        case State::initialized:
            return true;
        case State::invalid:
            return false;
    }
    if (verbose) state.log("PrebuiltLoader::isValid(%s)\n", this->leafName());

    // make an array of all Loaders in beingValidated state
    STACK_ALLOC_OVERFLOW_SAFE_ARRAY(const PrebuiltLoader*, loadersBeingValidated, 1024);
    if ( this->ref.app ) {
        // only examine processPrebuiltLoaderSet if Loader being validated is in processPrebuiltLoaderSet
        const PrebuiltLoaderSet* appDylibsSet = state.processPrebuiltLoaderSet();
        for ( uint32_t i = 0; i < appDylibsSet->loadersArrayCount; ++i ) {
            const PrebuiltLoader* ldr = appDylibsSet->atIndex(i);
            if ( ldr->loaderState(state) == State::beingValidated ) {
                loadersBeingValidated.push_back(ldr);
            }
        }
    }
    const PrebuiltLoaderSet* cachedDylibsSet = state.cachedDylibsPrebuiltLoaderSet();
    for ( uint32_t i = 0; i < cachedDylibsSet->loadersArrayCount; ++i ) {
        const PrebuiltLoader* ldr = cachedDylibsSet->atIndex(i);
        if ( ldr->loaderState(state) == State::beingValidated ) {
            loadersBeingValidated.push_back(ldr);
        }
    }
    if (verbose) state.log("   have %lu beingValidated Loaders\n", loadersBeingValidated.count());

    // look at each individual dylib in beingValidated state to see if it has an override file
    for (const PrebuiltLoader* ldr : loadersBeingValidated) {
        ldr->invalidateInIsolation(state);
    }

    // now keep propagating invalidness until nothing changes
    bool more = true;
    while (more) {
        more = false;
        if (verbose) state.log("checking shallow for %lu loaders\n", loadersBeingValidated.count());
        for (const PrebuiltLoader* ldr : loadersBeingValidated) {
            State&      ldrState    = ldr->loaderState(state);
            const State ldrOrgState = ldrState;
            if ( ldrOrgState == State::beingValidated ) {
                if (verbose) state.log("   invalidateShallow(%s)\n", ldr->leafName());
                ldr->invalidateShallow(state);
                if ( ldrState != ldrOrgState ) {
                   if (verbose) state.log("     %s state changed\n", ldr->leafName());
                   more = true;
                }
            }
        }
    }

    // mark everything left in beingValidate as valid (notMapped)
    for (const PrebuiltLoader* ldr : loadersBeingValidated) {
        if ( ldr->loaderState(state) == State::beingValidated )
            ldr->loaderState(state) = State::notMapped;
    }

    return (this->loaderState(state) != State::invalid);
}


// look to see if anything this loader directly depends on is invalid
void PrebuiltLoader::invalidateShallow(const RuntimeState& state) const
{
   for (int depIndex = 0; depIndex < this->depCount; ++depIndex) {
        if ( const Loader* dep = this->dependent(state, depIndex) ) {
            if ( dep->isPrebuilt ) {
                const PrebuiltLoader* pbDep = (PrebuiltLoader*)dep;
                State& depState = pbDep->loaderState(state);
                if ( depState == State::invalid ) {
                    this->loaderState(state) = State::invalid;
                }
            }
        }
    }
}

// just look to see if this one file is overridden
void PrebuiltLoader::invalidateInIsolation(const RuntimeState& state) const
{
    State& ldrState = this->loaderState(state);
    if ( ldrState == State::invalid )
        return;
    if ( ldrState >= State::notMapped )
        return;

    // validate the source file has not changed
    if ( this->dylibInDyldCache ) {
        if ( state.config.dyldCache.addr == nullptr ) {
            ldrState = State::invalid;
            return;
        }
#if BUILDING_DYLD
        // check for roots that override this dylib in the dyld cache
        if ( this->isOverridable ) {
            __block bool hasOnDiskOverride = false;
            bool stop = false;
            state.config.pathOverrides.forEachPathVariant(this->path(), state.config.process.platform, false, stop,
                                                      ^(const char* possiblePath, ProcessConfig::PathOverrides::Type type, bool& innerStop) {
                                                          // look only at variants that might override the original path
                                                          if ( type > ProcessConfig::PathOverrides::Type::rawPath ) {
                                                              innerStop = true;
                                                              return;
                                                          }
                                                          FileID foundFileID = FileID::none();
                                                          if ( state.config.fileExists(possiblePath, &foundFileID) ) {
                                                              FileID recordedFileID = this->fileID();
                                                              // Note: sim caches will have valid() fileIDs, others won't
                                                              if ( recordedFileID.valid() ) {
                                                                  if ( foundFileID != recordedFileID ) {
                                                                      if ( state.config.log.loaders )
                                                                          console("found '%s' with different inode/mtime than PrebuiltLoader for '%s'\n", possiblePath, this->path());
                                                                      hasOnDiskOverride = true;
                                                                      innerStop         = true;
                                                                  }
                                                              }
                                                              else {
                                                                  // this Loader had no recorded FileID, so it was not expected on disk, but now a file showed up
                                                                  if ( state.config.log.loaders )
                                                                      console("found '%s' which invalidates PrebuiltLoader for '%s'\n", possiblePath, this->path());
                                                                  hasOnDiskOverride = true;
                                                                  innerStop         = true;
                                                              }
                                                          }
                                                      });
            if ( hasOnDiskOverride ) {
                if ( state.config.log.loaders )
                    console("PrebuiltLoader %p '%s' not used because a file was found that overrides it\n", this, this->leafName());
                // PrebuiltLoader is for dylib in cache, but have one on disk that overrides cache
                ldrState = State::invalid;
                return;
            }
        }
#endif
    }
    else {
        // not in dyld cache
        FileID recordedFileID = this->fileID();
        if ( recordedFileID.valid() ) {
            // have recorded file inode (such as for embedded framework in 3rd party app)
            FileID foundFileID = FileID::none();
            if ( state.config.syscall.fileExists(this->path(), &foundFileID) ) {
                if ( foundFileID != recordedFileID ) {
                    ldrState = State::invalid;
                    if ( state.config.log.loaders )
                        console("PrebuiltLoader %p not used because file inode/mtime does not match\n", this);
                }
            }
            else {
                ldrState = State::invalid;
                if ( state.config.log.loaders )
                    console("PrebuiltLoader %p not used because file missing\n", this);
            }
        }
        else {
            // PrebuildLoaderSet did not record inode, check cdhash
            const char* path = this->path();
            // skip over main exectuable.  It's cdHash is checked as part of initializeClosureMode()
            if ( strcmp(path, state.config.process.mainExecutablePath) != 0 ) {
                int fd = state.config.syscall.open(path, O_RDONLY, 0);
                if ( fd != -1 ) {
                    Diagnostics cdHashDiag;
                    if ( Loader::validateFile(cdHashDiag, state, fd, path, this->codeSignature, *this->fileValidationInfo()) == (uint64_t)(-1) ) {
                        ldrState = State::invalid;
                        if ( state.config.log.loaders )
                            console("PrebuiltLoader %p not used because file '%s' cdHash changed\n", this, path);
                    }
                    state.config.syscall.close(fd);
                }
                else {
                    ldrState = State::invalid;
                    if ( state.config.log.loaders )
                        console("PrebuiltLoader %p not used because file '%s' cannot be opened\n", this, path);
                }
            }
        }
    }
}

Array<Loader::Region> PrebuiltLoader::segments() const
{
    return Array<Region>((Region*)((uint8_t*)this + regionsOffset), regionsCount, regionsCount);
}

const Array<PrebuiltLoader::BindTargetRef> PrebuiltLoader::bindTargets() const
{
    return Array<BindTargetRef>((BindTargetRef*)((uint8_t*)this + bindTargetRefsOffset), bindTargetRefsCount, bindTargetRefsCount);
}

const Array<PrebuiltLoader::BindTargetRef> PrebuiltLoader::overrideBindTargets() const
{
    return Array<BindTargetRef>((BindTargetRef*)((uint8_t*)this + overrideBindTargetRefsOffset), overrideBindTargetRefsCount, overrideBindTargetRefsCount);
}

bool PrebuiltLoader::hasBeenFixedUp(RuntimeState& state) const
{
    State& ldrState = this->loaderState(state);
    return ldrState >= State::fixedUp;
}

bool PrebuiltLoader::beginInitializers(RuntimeState& state)
{
    // do nothing if already initializers already run
    State& ldrState = this->loaderState(state);
    if ( ldrState == State::initialized )
        return true;
    if ( ldrState == State::beingInitialized )
        return true;

    assert(ldrState == State::fixedUp);

    // switch to being-inited state
    ldrState = State::beingInitialized;
    return false;
}

void PrebuiltLoader::runInitializers(RuntimeState& state) const
{
    // most images do not have initializers, so we make that case fast
    if ( this->hasInitializers ) {
        this->findAndRunAllInitializers(state);
    }
    this->loaderState(state) = State::initialized;
}

void PrebuiltLoader::setLoadAddress(RuntimeState& state, const MachOLoaded* ml) const
{
    assert(this->ref.app && "shared cache addresses are fixed");
    state.setAppLoadAddress(this->ref.index, ml);
}

////////////////////////  other functions /////////////////////////////////

PrebuiltLoader::PrebuiltLoader(const Loader& jitLoader)
    : Loader(InitialOptions(jitLoader), true)
{
}

size_t PrebuiltLoader::size() const
{
    return this->regionsOffset + this->regionsCount * sizeof(Region);
}

const Loader::FileValidationInfo* PrebuiltLoader::fileValidationInfo() const
{
    return (FileValidationInfo*)((uint8_t*)this + this->fileValidationOffset);
}

PrebuiltLoader::State& PrebuiltLoader::loaderState(const RuntimeState& state) const
{
    assert(sizeof(State) == sizeof(uint8_t));
    const uint8_t* stateArray = state.prebuiltStateArray(this->ref.app);
    return *((PrebuiltLoader::State*)&stateArray[this->ref.index]);
 }

////////////////////////////// ObjCBinaryInfo ///////////////////////////////////////

const ObjCBinaryInfo* PrebuiltLoader::objCBinaryInfo() const
{
    if ( this->objcBinaryInfoOffset == 0 )
        return nullptr;
    return (ObjCBinaryInfo*)((uint8_t*)this + this->objcBinaryInfoOffset);
}

void PrebuiltLoader::applyObjCFixups(RuntimeState& state) const
{
    const ObjCBinaryInfo* fixupInfo = objCBinaryInfo();
    if ( fixupInfo == nullptr )
        return;

    const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)this->loadAddress(state);
    const uint8_t* baseAddress = (const uint8_t*)ma;
    const uint32_t pointerSize = this->loadAddress(state)->pointerSize();

    // imageInfoRuntimeOffset.  This is always set it we have objc
    {
        uintptr_t* fixUpLoc = (uintptr_t*)(baseAddress + fixupInfo->imageInfoRuntimeOffset);
        MachOAnalyzer::ObjCImageInfo* imageInfo = (MachOAnalyzer::ObjCImageInfo *)fixUpLoc;
        ((MachOAnalyzer::ObjCImageInfo*)imageInfo)->flags |= MachOAnalyzer::ObjCImageInfo::dyldPreoptimized;
        if ( state.config.log.fixups )
            state.log("fixup: *0x%012lX = 0x%012lX <objc-info preoptimized>\n", (uintptr_t)fixUpLoc, *fixUpLoc);
    }

    const dyld3::MachOAnalyzer::VMAddrConverter& vmAddrConverter = ma->makeVMAddrConverter(true);
    const uint64_t loadAddress = ma->preferredLoadAddress();

    // Protocols.
    // If we have only a single definition of a protocol, then that definition should be fixed up.
    // If we have multiple definitions of a protocol, then we should fix up just the first one we see.
    // Only the first is considered the canonical definition.
    if ( fixupInfo->protocolFixupsOffset != 0 ) {
        // Get the pointer to the Protocol class.
        uint64_t classProtocolPtr = (uint64_t)state.config.dyldCache.addr + state.processPrebuiltLoaderSet()->objcProtocolClassCacheOffset;

        Array<uint8_t> protocolFixups = fixupInfo->protocolFixups();
        __block uint32_t protocolIndex = 0;
        auto visitProtocol = ^(uint64_t protocolVMAddr, const dyld3::MachOAnalyzer::ObjCProtocol& objCProtocol, bool& stop) {
            bool isCanonical = protocolFixups[protocolIndex++] == 1;
            if ( isCanonical ) {
                uint64_t runtimeOffset = protocolVMAddr - loadAddress;
                uintptr_t* fixUpLoc = (uintptr_t*)(baseAddress + runtimeOffset);
                uintptr_t value = (uintptr_t)classProtocolPtr;
    #if __has_feature(ptrauth_calls)
                // Sign the ISA on arm64e.
                // Unfortunately a hard coded value here is not ideal, but this is ABI so we aren't going to change it
                // This matches the value in libobjc __objc_opt_ptrs: .quad x@AUTH(da, 27361, addr)
                value = MachOLoaded::ChainedFixupPointerOnDisk::Arm64e::signPointer(value, fixUpLoc, true, 27361, 2);
    #endif
                if ( state.config.log.fixups )
                    state.log("fixup: *0x%012lX = 0x%012lX <objc-protocol>\n", (uintptr_t)fixUpLoc, (uintptr_t)value);
                *fixUpLoc = value;
            }
        };
        ma->forEachObjCProtocol(fixupInfo->protocolListRuntimeOffset, fixupInfo->protocolListCount, vmAddrConverter, visitProtocol);
    }

    // Selectors
    if ( fixupInfo->selectorReferencesFixupsCount != 0 ) {
        const objc_opt::objc_opt_t* opts = state.config.dyldCache.objCCacheInfo;

        // The selector table changed in version 16.  For now, support both tables
        const legacy_objc_opt::objc_selopt_t* legacyCacheHashTable = nullptr;
        const objc::SelectorHashTable* dyldCacheHashTable = nullptr;
        if ( state.config.dyldCache.objCCacheInfo->oldClassOpt() )
            legacyCacheHashTable = (legacy_objc_opt::objc_selopt_t*)opts->selectorOpt();
        else
            dyldCacheHashTable = opts->selectorOpt();

        Array<BindTargetRef> selectorReferenceFixups = fixupInfo->selectorReferenceFixups();
        __block uint32_t fixupIndex = 0;
        PrebuiltObjC::forEachSelectorReferenceToUnique(state, ma, loadAddress, *fixupInfo, vmAddrConverter,
                                                       ^(uint64_t selectorReferenceRuntimeOffset, uint64_t selectorStringRuntimeOffset) {
            const BindTargetRef& bindTargetRef = selectorReferenceFixups[fixupIndex++];

            const char* selectorString = nullptr;
            if ( bindTargetRef.isAbsolute() ) {
                // HACK!: We use absolute bind targets as indices in to the shared cache table, not actual absolute fixups
                if ( dyldCacheHashTable != nullptr )
                    selectorString = dyldCacheHashTable->getEntryForIndex((uint32_t)bindTargetRef.value(state));
                else
                    selectorString = legacyCacheHashTable->getEntryForIndex((uint32_t)bindTargetRef.value(state));
            } else {
                // For the app case, we just point directly to the image containing the selector
                selectorString = (const char*)bindTargetRef.value(state);
            }
            uintptr_t* fixUpLoc = (uintptr_t*)(baseAddress + selectorReferenceRuntimeOffset);
            uintptr_t value = (uintptr_t)selectorString;
            if ( state.config.log.fixups )
                state.log("fixup: *0x%012lX = 0x%012lX <objc-selector '%s'>\n", (uintptr_t)fixUpLoc, (uintptr_t)value, (const char*)value);
            *fixUpLoc = value;
        });
    }

    // Stable Swift Classes
    if ( fixupInfo->hasClassStableSwiftFixups ) {
        dyld3::MachOAnalyzer::ClassCallback visitClass = ^(uint64_t classVMAddr,
                                                           uint64_t classSuperclassVMAddr, uint64_t classDataVMAddr,
                                                           const MachOAnalyzer::ObjCClassInfo &objcClass, bool isMetaClass,
                                                           bool& stop) {
            if ( isMetaClass )
                return;

            // Does this class need to be fixed up for stable Swift ABI.
            if ( objcClass.isUnfixedBackwardDeployingStableSwift() ) {
                // Class really is stable Swift, pretending to be pre-stable.
                // Fix its lie.  This involves fixing the FAST bits on the class data value
                uint64_t runtimeOffset = classDataVMAddr - loadAddress;
                uintptr_t* fixUpLoc = (uintptr_t*)(baseAddress + runtimeOffset);
                uintptr_t value = ((*fixUpLoc) | MachOAnalyzer::ObjCClassInfo::FAST_IS_SWIFT_STABLE) & ~MachOAnalyzer::ObjCClassInfo::FAST_IS_SWIFT_LEGACY;
                if ( state.config.log.fixups )
                    state.log("fixup: *0x%012lX = 0x%012lX <mark swift stable>\n", (uintptr_t)fixUpLoc, (uintptr_t)value);
                *fixUpLoc = value;
            }
        };
        ma->forEachObjCClass(fixupInfo->classListRuntimeOffset, fixupInfo->classListCount, vmAddrConverter, visitClass);
    }

    // Method lists to set as uniqued.

    // This is done for all pointer-based method lists.  Relative method lists should already be uniqued as they point to __objc_selrefs
    auto trySetMethodListAsUniqued = [&](uint64_t methodListVMAddr) {
        if ( methodListVMAddr == 0 )
            return;

        uint64_t methodListRuntimeOffset = methodListVMAddr - loadAddress;
        if ( ma->objcMethodListIsRelative(methodListRuntimeOffset) )
            return;

        // Set the method list to have the uniqued bit set
        uint32_t* fixUpLoc = (uint32_t*)(baseAddress + methodListRuntimeOffset);
        uint32_t value = (*fixUpLoc) | MachOAnalyzer::ObjCMethodList::methodListIsUniqued;
        if ( state.config.log.fixups )
            state.log("fixup: *0x%012lX = 0x%012lX <mark method list uniqued>\n", (uintptr_t)fixUpLoc, (uintptr_t)value);
        *fixUpLoc = value;
    };

    // Class method lists
    if ( fixupInfo->hasClassMethodListsToSetUniqued ) {
        dyld3::MachOAnalyzer::ClassCallback visitClass = ^(uint64_t classVMAddr,
                                                           uint64_t classSuperclassVMAddr, uint64_t classDataVMAddr,
                                                           const MachOAnalyzer::ObjCClassInfo &objcClass, bool isMetaClass,
                                                           bool& stop) {
            trySetMethodListAsUniqued(objcClass.baseMethodsVMAddr(pointerSize));
        };
        ma->forEachObjCClass(fixupInfo->classListRuntimeOffset, fixupInfo->classListCount, vmAddrConverter, visitClass);
    }

    // Category method lists
    if ( fixupInfo->hasCategoryMethodListsToSetUniqued ) {
        auto visitCategory = ^(uint64_t categoryVMAddr, const dyld3::MachOAnalyzer::ObjCCategory& objcCategory, bool& stop) {
            trySetMethodListAsUniqued(objcCategory.instanceMethodsVMAddr);
            trySetMethodListAsUniqued(objcCategory.classMethodsVMAddr);
        };
        ma->forEachObjCCategory(fixupInfo->categoryListRuntimeOffset, fixupInfo->categoryCount, vmAddrConverter, visitCategory);
    }

    // Protocol method lists
    if ( fixupInfo->hasProtocolMethodListsToSetUniqued ) {
        auto visitProtocol = ^(uint64_t protocolVMAddr, const dyld3::MachOAnalyzer::ObjCProtocol& objCProtocol, bool& stop) {
            trySetMethodListAsUniqued(objCProtocol.instanceMethodsVMAddr);
            trySetMethodListAsUniqued(objCProtocol.classMethodsVMAddr);
            trySetMethodListAsUniqued(objCProtocol.optionalInstanceMethodsVMAddr);
            trySetMethodListAsUniqued(objCProtocol.optionalClassMethodsVMAddr);
        };
        ma->forEachObjCProtocol(fixupInfo->protocolListRuntimeOffset, fixupInfo->protocolListCount, vmAddrConverter, visitProtocol);
    }
}

void PrebuiltLoader::printObjCFixups(RuntimeState& state, FILE* out) const
{
    const ObjCBinaryInfo* fixupInfo = objCBinaryInfo();
    if ( fixupInfo == nullptr )
        return;

    // imageInfoRuntimeOffset.  This is always set it we have objc
    {
        fprintf(out, ",\n");
        fprintf(out, "      \"objc-image-info-offset\":    \"0x%llX\"", fixupInfo->imageInfoRuntimeOffset);
    }


    // Protocols
    if ( fixupInfo->protocolFixupsOffset != 0 ) {
        fprintf(out, ",\n      \"objc-canonical-protocols\": [");
        Array<uint8_t> protocolFixups = fixupInfo->protocolFixups();
        bool needComma = false;
        for ( uint8_t isCanonical : protocolFixups ) {
            if ( needComma )
                fprintf(out, ",");
            fprintf(out, "\n          \"%s\"", (isCanonical == 1) ? "true" : "false");
            needComma = true;
        }
        fprintf(out, "\n      ]");
    }

    // Selectors
    if ( fixupInfo->selectorReferencesFixupsCount != 0 ) {
        fprintf(out, ",\n      \"objc-selectors\": [");
        bool needComma = false;
        for ( const BindTargetRef& target : fixupInfo->selectorReferenceFixups() ) {
            if ( needComma )
                fprintf(out, ",");
            fprintf(out, "\n          {\n");
            if ( target.isAbsolute() ) {
                // HACK!: We use absolute bind targets as indices in to the shared cache table, not actual absolute fixups
                fprintf(out, "              \"shared-selector-index\":    \"0x%llX\"\n", target.value(state));
            }
            else {
                fprintf(out, "              \"loader\":   \"%c.%d\",\n", target.loaderRef().app ? 'a' : 'c', target.loaderRef().index);
                fprintf(out, "              \"offset\":   \"0x%08llX\"\n", target.offset());
            }
            fprintf(out, "          }");
            needComma = true;
        }
        fprintf(out, "\n      ]");
    }
}

void PrebuiltLoader::serialize(Diagnostics& diag, RuntimeState& state, const DyldSharedCache* cache, const JustInTimeLoader& jitLoader, LoaderRef buildRef,
                               CacheWeakDefOverride cacheWeakDefFixup, const PrebuiltObjC& prebuiltObjC, BumpAllocator& allocator)
{
    // use allocator and placement new to instantiate PrebuiltLoader object
    size_t serializationStart = allocator.size();
    allocator.zeroFill(sizeof(PrebuiltLoader));
    BumpAllocatorPtr<PrebuiltLoader> p(allocator, serializationStart);
    new (p.get()) PrebuiltLoader(jitLoader);
    p->ref                  = buildRef;

    // record offset of load command that specifies fixups (LC_DYLD_INFO or LC_DYLD_CHAINED_FIXUPS)
    const MachOAnalyzer* ma    = (MachOAnalyzer*)(jitLoader.loadAddress(state));
    p->fixupsLoadCommandOffset = ma->getFixupsLoadCommandFileOffset();

    // append path to serialization
    p->pathOffset    = allocator.size() - serializationStart;
    const char* path = jitLoader.path();
    allocator.append(path, strlen(path) + 1);
    p->altPathOffset            = 0;
    const char* installNamePath = ma->installName();
    if ( ma->isDylib() && (strcmp(installNamePath, path) != 0) ) {
        p->altPathOffset = allocator.size() - serializationStart;
        allocator.append(installNamePath, strlen(installNamePath) + 1);
    }

    // on customer installs, most dylibs in cache are not overridable
    p->isOverridable = jitLoader.dylibInDyldCache && ((cache->header.cacheType == kDyldSharedCacheTypeDevelopment) || cache->isOverridablePath(path));

    // append dependents to serialization
    uint32_t depCount = jitLoader.dependentCount();
    p->depCount = depCount;
    allocator.align(sizeof(LoaderRef));
    uint16_t depLoaderRefsArrayOffset = allocator.size() - serializationStart;
    p->dependentLoaderRefsArrayOffset = depLoaderRefsArrayOffset;
    allocator.zeroFill(depCount * sizeof(LoaderRef));
    BumpAllocatorPtr<LoaderRef> depArray(allocator, serializationStart + depLoaderRefsArrayOffset);
    Loader::DependentKind kinds[depCount+1];
    bool                  hasNonRegularLink = false;
    for ( uint32_t depIndex = 0; depIndex < depCount; ++depIndex ) {
        Loader* depLoader = jitLoader.dependent(state, depIndex, &kinds[depIndex]);
        if ( kinds[depIndex] != Loader::DependentKind::normal )
            hasNonRegularLink = true;
        if ( depLoader == nullptr ) {
            assert(kinds[depIndex] == Loader::DependentKind::weakLink);
            depArray.get()[depIndex] = LoaderRef::missingWeakImage();
        }
        else {
            depArray.get()[depIndex] = depLoader->ref;
        }
    }

    // if any non-regular linking of dependents, append array for that
    p->dependentKindArrayOffset = 0;
    if ( hasNonRegularLink ) {
        static_assert(sizeof(Loader::DependentKind) == 1, "DependentKind expect to be one byte");
        uint16_t dependentKindArrayOff = allocator.size() - serializationStart;
        p->dependentKindArrayOffset    = dependentKindArrayOff;
        allocator.zeroFill(depCount * sizeof(Loader::DependentKind));
        BumpAllocatorPtr<Loader::DependentKind> kindArray(allocator, serializationStart + dependentKindArrayOff);
        memcpy(kindArray.get(), kinds, depCount * sizeof(Loader::DependentKind));
    }

    // record exports-trie location
    jitLoader.getExportsTrie(p->exportsTrieLoaderOffset, p->exportsTrieLoaderSize);

    // just record if image has any initializers (but not what they are)
    p->hasInitializers = ma->hasInitializer(diag);
    if ( diag.hasError() )
        return;

    // record code signature location
    p->codeSignature.fileOffset = 0;
    p->codeSignature.size       = 0;
    if ( !jitLoader.dylibInDyldCache ) {
        uint32_t sigFileOffset;
        uint32_t sigSize;
        if ( ma->hasCodeSignature(sigFileOffset, sigSize) ) {
            p->codeSignature.fileOffset = sigFileOffset;
            p->codeSignature.size       = sigSize;
        }
    }

    // append FileValidationInfo
    if ( !jitLoader.dylibInDyldCache || cache->header.dylibsExpectedOnDisk ) {
        allocator.align(__alignof__(FileValidationInfo));
        FileValidationInfo info = jitLoader.getFileValidationInfo();
        uintptr_t          off  = allocator.size() - serializationStart;
        p->fileValidationOffset = off;
        assert(p->fileValidationOffset == off && "uint16_t fileValidationOffset overflow");
        allocator.append(&info, sizeof(FileValidationInfo));
    }

    // append segments to serialization
    p->vmSpace = (uint32_t)ma->mappedSize();
    jitLoader.withRegions(ma, ^(const Array<Region>& regions) {
        allocator.align(__alignof__(Region));
        uintptr_t off    = allocator.size() - serializationStart;
        p->regionsOffset = off;
        assert(p->regionsOffset == off && "uint16_t regionsOffset overflow");
        p->regionsCount = regions.count();
        allocator.append(&regions[0], sizeof(Region) * regions.count());
    });

    // add catalyst support info
    bool buildingMacOSCache = jitLoader.dylibInDyldCache && ((dyld3::Platform)(cache->header.platform) == dyld3::Platform::macOS);
    p->supportsCatalyst = buildingMacOSCache && ma->builtForPlatform(dyld3::Platform::iOSMac);
    p->overridesCache   = false;
    p->indexOfTwin      = kNoUnzipperedTwin;
    p->reserved1        = 0;
    if ( buildingMacOSCache ) {
        // check if this is part of an unzippered twin
        if ( !p->supportsCatalyst ) {
            char catalystTwinPath[PATH_MAX];
            strlcpy(catalystTwinPath, "/System/iOSSupport", PATH_MAX);
            strlcat(catalystTwinPath, path, PATH_MAX);
            for (const Loader* ldr : state.loaded) {
                if ( ldr->matchesPath(catalystTwinPath) ) {
                    // record index of catalyst side in mac side
                    p->indexOfTwin = ldr->ref.index;
                    break;
                }
            }
        }
        else if ( strncmp(path, "/System/iOSSupport/", 19) == 0 ) {
            const char* macTwinPath = &path[18];
            for (const Loader* ldr : state.loaded) {
                if ( ldr->matchesPath(macTwinPath) ) {
                    // record index of mac side in catalyst side
                    p->indexOfTwin    = ldr->ref.index;
                    p->overridesCache = true;  // catalyst side of twin (if used) is an override of the mac side
                    break;
                }
            }
        }
    }

    // append fixup target info to serialization
    // note: this can be very large, so it is last in the small layout so that uint16_t to other things don't overflow
    STACK_ALLOC_OVERFLOW_SAFE_ARRAY(BindTargetRef, overrideBindTargets, 16);
    if ( !jitLoader.dylibInDyldCache ) {
        allocator.align(__alignof__(BindTargetRef));
        uintptr_t off           = allocator.size() - serializationStart;
        p->bindTargetRefsOffset = off;
        assert(p->bindTargetRefsOffset == off && "uint16_t bindTargetRefsOffset overflow");
        p->bindTargetRefsCount = 0;
        jitLoader.forEachBindTarget(diag, state, cacheWeakDefFixup, true, ^(const ResolvedSymbol& resolvedTarget, bool& stop) {
            // Regular and lazy binds
            BindTargetRef bindRef(resolvedTarget);
            allocator.append(&bindRef, sizeof(BindTargetRef));
            p->bindTargetRefsCount += 1;
            assert(p->bindTargetRefsCount != 0 && "bindTargetRefsCount overflow");
        }, ^(const ResolvedSymbol& resolvedTarget, bool& stop) {
            // Opcode based weak binds
            BindTargetRef bindRef(resolvedTarget);
            overrideBindTargets.push_back(bindRef);
        });
    }

    // Everything from this point onwards needs 32-bit offsets
    if ( !overrideBindTargets.empty() ) {
        allocator.align(__alignof__(BindTargetRef));
        uintptr_t off           = allocator.size() - serializationStart;
        p->overrideBindTargetRefsOffset = (uint32_t)off;
        p->overrideBindTargetRefsCount = (uint32_t)overrideBindTargets.count();
        allocator.append(&overrideBindTargets[0], sizeof(BindTargetRef) * overrideBindTargets.count());
    }

    // append ObjCFixups
    uint32_t objcFixupsOffset = prebuiltObjC.serializeFixups(jitLoader, allocator);
    p->objcBinaryInfoOffset = (objcFixupsOffset == 0) ? 0 : (objcFixupsOffset - (uint32_t)serializationStart);

    // append patch table
    p->patchTableOffset = 0;
    const DylibPatch*   patchTable;
    uint16_t            cacheDylibOverriddenIndex;
    if ( jitLoader.overridesDylibInCache(patchTable, cacheDylibOverriddenIndex) ) {
        if ( patchTable != nullptr ) {
            p->patchTableOffset = (uint32_t)(allocator.size() - serializationStart);
            uint32_t patchTableSize = sizeof(DylibPatch);
            for ( const DylibPatch* patch = patchTable; patch->overrideOffsetOfImpl != -1; ++patch )
                patchTableSize += sizeof(DylibPatch);
            allocator.append(patchTable, patchTableSize);
        }
    }
}

bool PrebuiltLoader::overridesDylibInCache(const DylibPatch*& patchTable, uint16_t& cacheDylibOverriddenIndex) const
{
    if ( !this->overridesCache )
        return false;

    patchTable                = (this->patchTableOffset == 0) ? nullptr : (DylibPatch*)(((uint8_t*)this) + this->patchTableOffset);
    cacheDylibOverriddenIndex = this->indexOfTwin;
    return true;
}

// Prints a string with any special characters delimited
static void printJSONString(FILE* out, const char* str)
{
    while ( *str != '\0' ) {
        char c = *str;
        if (c == '"')
            fputc('\\', out);
        fputc(c, out);
        ++str;
    }
}

void PrebuiltLoader::print(RuntimeState& state, FILE* out, bool printComments) const
{
    fprintf(out, "    {\n");
    fprintf(out, "      \"path\":    \"");
    printJSONString(out, path());
    fprintf(out, "\",\n");
    if ( altPathOffset != 0 ) {
        fprintf(out, "      \"path-alt\":    \"");
        printJSONString(out, (char*)this + this->altPathOffset);
        fprintf(out, "\",\n");
    }
    fprintf(out, "      \"loader\":  \"%c.%d\",\n", ref.app ? 'a' : 'c', ref.index);
    fprintf(out, "      \"vm-size\": \"0x%X\",\n", this->vmSpace);
    if ( this->dylibInDyldCache ) {
        fprintf(out, "      \"overridable\": \"%s\",\n", this->isOverridable ? "true" : "false");
        fprintf(out, "      \"supports-catalyst\": \"%s\",\n", this->supportsCatalyst ? "true" : "false");
        if ( this->indexOfTwin != kNoUnzipperedTwin ) {
            if ( this->supportsCatalyst )
                fprintf(out, "      \"mac-twin\": \"c.%d\",", this->indexOfTwin);
            else
                fprintf(out, "      \"catalyst-twin\": \"c.%d\",", this->indexOfTwin);
           if ( printComments ) {
                PrebuiltLoader::LoaderRef twinRef(false, this->indexOfTwin);
                const char* twinPath =  twinRef.loader(state)->path();
                fprintf(out, "     # %s", twinPath);
            }
            fprintf(out, "\n");
            if ( this->patchTableOffset != 0 ) {
                uint32_t patchTableSizeCount = 0;
                for ( const DylibPatch* patch = (DylibPatch*)(((uint8_t*)this) + this->patchTableOffset); patch->overrideOffsetOfImpl != -1; ++patch )
                    patchTableSizeCount++;
                fprintf(out, "      \"patch-table-entries\": \"%d\",\n", patchTableSizeCount);
            }
        }
    }
    fprintf(out, "      \"has-initializers\": \"%s\",\n", this->hasInitializers ? "true" : "false");
    bool needComma = false;
    fprintf(out, "      \"segments\": [");
    for ( const Region& seg : this->segments() ) {
        if ( needComma )
            fprintf(out, ",");
        fprintf(out, "\n        {\n");
        fprintf(out, "          \"vm-offset\":       \"0x%llX\",\n", seg.vmOffset);
        fprintf(out, "          \"file-size\":       \"0x%X\",\n", seg.fileSize);
        fprintf(out, "          \"file-offset\":     \"0x%X\",\n", seg.fileOffset);
        char writeChar = (seg.perms & 2) ? 'w' : '-';
        if ( seg.readOnlyData )
            writeChar = 'W';
        fprintf(out, "          \"permissions\":     \"%c%c%c\"\n", ((seg.perms & 1) ? 'r' : '-'), writeChar, ((seg.perms & 4) ? 'x' : '-'));
        fprintf(out, "         }");
        needComma = true;
    }
    fprintf(out, "\n      ],\n");

    if ( this->fileValidationOffset != 0 ) {
        const FileValidationInfo* fileInfo = this->fileValidationInfo();
        fprintf(out, "      \"file-info\":  {\n");
        if ( fileInfo->checkInodeMtime ) {
            fprintf(out, "          \"slice-offset\":    \"0x%llX\",\n", fileInfo->sliceOffset);
            fprintf(out, "          \"inode\":           \"0x%llX\",\n", fileInfo->inode);
            fprintf(out, "          \"mod-time\":        \"0x%llX\",\n", fileInfo->mtime);
        }
        fprintf(out, "          \"code-sig-offset\": \"0x%X\",\n", this->codeSignature.fileOffset);
        fprintf(out, "          \"code-sig-size\":   \"0x%X\",\n", this->codeSignature.size);
        if ( fileInfo->checkCdHash ) {
            fprintf(out, "          \"cd-hash\":         \"%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\"\n",
                fileInfo->cdHash[0], fileInfo->cdHash[1], fileInfo->cdHash[2], fileInfo->cdHash[3],
                fileInfo->cdHash[4], fileInfo->cdHash[5], fileInfo->cdHash[6], fileInfo->cdHash[7],
                fileInfo->cdHash[8], fileInfo->cdHash[9], fileInfo->cdHash[10], fileInfo->cdHash[11],
                fileInfo->cdHash[12], fileInfo->cdHash[13], fileInfo->cdHash[14], fileInfo->cdHash[15],
                fileInfo->cdHash[16], fileInfo->cdHash[17], fileInfo->cdHash[18], fileInfo->cdHash[19]);
        }
        fprintf(out, "       },\n");
    }

    if ( exportsTrieLoaderOffset != 0 ) {
        fprintf(out, "      \"exports-trie\":  {\n");
        fprintf(out, "          \"vm-offset\":      \"0x%llX\",\n", exportsTrieLoaderOffset);
        fprintf(out, "          \"size\":           \"0x%X\"\n", exportsTrieLoaderSize);
        fprintf(out, "      },\n");
    }

    fprintf(out, "      \"dependents\": [");
    const PrebuiltLoader::LoaderRef* depsArray = (PrebuiltLoader::LoaderRef*)((uint8_t*)this + this->dependentLoaderRefsArrayOffset);
    needComma                                  = false;
    for ( uint32_t depIndex = 0; depIndex < this->depCount; ++depIndex ) {
        if ( needComma )
            fprintf(out, ",");
        PrebuiltLoader::LoaderRef dep     = depsArray[depIndex];
        const char*               kindStr = "regular";
        if ( this->dependentKindArrayOffset != 0 ) {
            const DependentKind* kindsArray = (DependentKind*)((uint8_t*)this + this->dependentKindArrayOffset);
            switch ( kindsArray[depIndex] ) {
                case DependentKind::normal:
                    break;
                case DependentKind::weakLink:
                    kindStr = "weak";
                    break;
                case DependentKind::upward:
                    kindStr = "upward";
                    break;
                case DependentKind::reexport:
                    kindStr = "reexport";
                    break;
                default:
                    kindStr = "???";
                    break;
            }
        }
        const char* depPath = dep.isMissingWeakImage() ? "missing weak link" : dep.loader(state)->path();
        fprintf(out, "\n          {\n");
        fprintf(out, "              \"kind\":           \"%s\",\n", kindStr);
        fprintf(out, "              \"loader\":         \"%c.%d\"", dep.app ? 'a' : 'c', dep.index);
        if ( printComments )
            fprintf(out, "     # %s\n", depPath);
        else
            fprintf(out, "\n");
        fprintf(out, "          }");
        needComma = true;
    }
    fprintf(out, "\n      ]");
    if ( bindTargetRefsOffset != 0 ) {
        fprintf(out, ",\n      \"targets\": [");
        needComma = false;
        for ( const BindTargetRef& target : this->bindTargets() ) {
            if ( needComma )
                fprintf(out, ",");
            fprintf(out, "\n          {\n");
            if ( target.isAbsolute() ) {
                fprintf(out, "              \"absolute-value\":    \"0x%llX\"\n", target.value(state));
            }
            else {
                fprintf(out, "              \"loader\":   \"%c.%d\",", target.loaderRef().app ? 'a' : 'c', target.loaderRef().index);
                if ( printComments )
                    fprintf(out, "        # %s\n", target.loaderRef().loader(state)->path());
                else
                    fprintf(out, "\n");
                fprintf(out, "              \"offset\":   \"0x%08llX\"\n", target.offset());
            }
            fprintf(out, "          }");
            needComma = true;
        }
        fprintf(out, "\n      ]");
    }

    if ( overrideBindTargetRefsOffset != 0 ) {
        fprintf(out, ",\n      \"override-targets\": [");
        needComma = false;
        for ( const BindTargetRef& target : this->overrideBindTargets() ) {
            if ( needComma )
                fprintf(out, ",");
            fprintf(out, "\n          {\n");
            if ( target.isAbsolute() ) {
                fprintf(out, "              \"absolute-value\":    \"0x%llX\"\n", target.value(state));
            }
            else {
                fprintf(out, "              \"loader\":   \"%c.%d\",", target.loaderRef().app ? 'a' : 'c', target.loaderRef().index);
                if ( printComments )
                    fprintf(out, "        # %s\n", target.loaderRef().loader(state)->path());
                else
                    fprintf(out, "\n");
                fprintf(out, "              \"offset\":   \"0x%08llX\"\n", target.offset());
            }
            fprintf(out, "          }");
            needComma = true;
        }
        fprintf(out, "\n      ]");
    }

    if ( objcBinaryInfoOffset != 0 )
        printObjCFixups(state, out);

    fprintf(out, "\n ");

    fprintf(out, "    }\n");
}


//
// MARK: --- PrebuiltLoaderSet methods ---
//

bool PrebuiltLoaderSet::hasValidMagic() const
{
    return (this->magic == kMagic);
}

bool PrebuiltLoaderSet::contains(const void* p, size_t pLen) const
{
    if ( (uint8_t*)p < (uint8_t*)this )
        return false;
    if ( ((uint8_t*)p + pLen) > ((uint8_t*)this + length) )
        return false;
    return true;
}


bool PrebuiltLoaderSet::validHeader(RuntimeState& state) const
{
    // verify this is the current PrebuiltLoaderSet format
    if ( !this->hasValidMagic() ) {
        if ( state.config.log.loaders )
            console("not using PrebuiltLoaderSet %p because magic at start does not match\n", this);
        return false;
    }
    if ( this->versionHash != PREBUILTLOADER_VERSION ) {
        if ( state.config.log.loaders ) {
            console("not using PrebuiltLoaderSet %p because versionHash (0x%08X) does not match dyld (0x%08X)\n",
                        this, this->versionHash, PREBUILTLOADER_VERSION);
        }
        return false;
    }
    return true;
}

bool PrebuiltLoaderSet::isValid(RuntimeState& state) const
{
    // verify this is the current PrebuiltLoaderSet format
    if ( !this->validHeader(state) )
        return false;

    // verify current dyld cache is same as when PrebuiltLoaderSet was built
    uuid_t expectedCacheUUID;
    if ( this->hasCacheUUID(expectedCacheUUID) ) {
        if ( const DyldSharedCache* cache = state.config.dyldCache.addr ) {
            uuid_t actualCacheUUID;
            cache->getUUID(actualCacheUUID);
            if ( ::memcmp(expectedCacheUUID, actualCacheUUID, sizeof(uuid_t)) != 0 ) {
                if ( state.config.log.loaders )
                    console("not using PrebuiltLoaderSet %p because cache UUID does not match\n", this);
                return false;
            }
        }
        else {
            // PrebuiltLoaderSet was built with a dyld cache, but this process does not have a cache
            if ( state.config.log.loaders )
                console("not using PrebuiltLoaderSet %p because process does not have a dyld cache\n", this);
            return false;
        }
    }

    // verify must-be-missing files are still missing
    __block bool missingFileShowedUp = false;
    this->forEachMustBeMissingPath(^(const char* path, bool& stop) {
        if ( state.config.syscall.fileExists(path) ) {
            if ( state.config.log.loaders )
                console("not using PrebuiltLoaderSet %p because existence of file '%s' invalids the PrebuiltLoaderSet\n", this, path);
            missingFileShowedUp = true;
            stop = true;
        }
    });
    if ( missingFileShowedUp )
        return false;

    // verify all PrebuiltLoaders in the set are valid
    bool somethingInvalid = false;
    for ( uint32_t i = 0; i < loadersArrayCount; ++i ) {
        const PrebuiltLoader* ldr = this->atIndex(i);
        if ( !ldr->isValid(state) )
            somethingInvalid = true;
    }
    return !somethingInvalid;
}

const PrebuiltLoader* PrebuiltLoaderSet::findLoader(const char* path) const
{
    uint16_t imageIndex;
    if ( this->findIndex(path, imageIndex) )
        return this->atIndex(imageIndex);
    return nullptr;
}

void PrebuiltLoaderSet::forEachMustBeMissingPath(void (^callback)(const char* path, bool& stop)) const
{
    bool stop = false;
    const char* path = (char*)this + this->mustBeMissingPathsOffset;
    for ( uint32_t i = 0; !stop && (i < this->mustBeMissingPathsCount); ++i ) {
        callback(path, stop);
        path += strlen(path)+1;
    }
}

bool PrebuiltLoaderSet::findIndex(const char* path, uint16_t& index) const
{
    for ( uint32_t i = 0; i < loadersArrayCount; ++i ) {
        const PrebuiltLoader* loader = this->atIndex(i);
        if ( strcmp(loader->path(), path) == 0 ) {
            index = i;
            return true;
        }
    }
    return false;
}

bool PrebuiltLoaderSet::hasCacheUUID(uuid_t uuid) const
{
    if ( this->dyldCacheUUIDOffset == 0 )
        return false;
    ::memcpy(uuid, (uint8_t*)this + this->dyldCacheUUIDOffset, sizeof(uuid_t));
    return true;
}

const ObjCSelectorOpt* PrebuiltLoaderSet::objcSelectorOpt() const {
    if ( this->objcSelectorHashTableOffset == 0 )
        return nullptr;
    return (const ObjCSelectorOpt*)((uint8_t*)this + this->objcSelectorHashTableOffset);
}

const ObjCClassOpt* PrebuiltLoaderSet::objcClassOpt() const {
    if ( this->objcClassHashTableOffset == 0 )
        return nullptr;
    return (const ObjCClassOpt*)((uint8_t*)this + this->objcClassHashTableOffset);
}

const ObjCClassOpt* PrebuiltLoaderSet::objcProtocolOpt() const {
    if ( this->objcProtocolHashTableOffset == 0 )
        return nullptr;
    return (const ObjCClassOpt*)((uint8_t*)this + this->objcProtocolHashTableOffset);
}

void PrebuiltLoaderSet::logDuplicateObjCClasses(RuntimeState& state) const
{
#if BUILDING_DYLD || BUILDING_UNIT_TESTS
    if ( const ObjCClassOpt* classesHashTable = objcClassOpt() ) {
        if ( !classesHashTable->hasDuplicates() || !state.config.log.initializers )
            return;

        // The main executable can contain a list of duplicates to ignore.
        const dyld3::MachOAnalyzer* mainMA = (const dyld3::MachOAnalyzer*)state.mainExecutableLoader->loadAddress(state);
        __block dyld3::CStringMapTo<bool> duplicateClassesToIgnore;
        mainMA->forEachObjCDuplicateClassToIgnore(^(const char *className) {
            duplicateClassesToIgnore[className] = true;
        });

        classesHashTable->forEachClass(state, ^(const PrebuiltLoader::BindTargetRef &nameTarget,
                                                const Array<PrebuiltLoader::BindTargetRef> &implTargets) {
            // Skip entries without duplicates
            if ( implTargets.count() == 1 )
                return;

            // The first target is the one we warn everyone else is a duplicate against
            const char* className = (const char*)nameTarget.value(state);
            if ( duplicateClassesToIgnore.find(className) != duplicateClassesToIgnore.end() )
                return;

            const char* oldPath = implTargets[0].loaderRef().loader(state)->path();
            const void* oldCls = (const void*)implTargets[0].value(state);
            for ( const PrebuiltLoader::BindTargetRef& implTarget : implTargets.subArray(1, implTargets.count() - 1) ) {
                const char* newPath = implTarget.loaderRef().loader(state)->path();
                const void* newCls = (const void*)implTarget.value(state);
                state.log("Class %s is implemented in both %s (%p) and %s (%p). "
                          "One of the two will be used. Which one is undefined.\n",
                          className, oldPath, oldCls, newPath, newCls);
            }
        });
    }
#endif
}

void PrebuiltLoaderSet::print(RuntimeState& state, FILE* out, bool printComments) const
{
    fprintf(out, "{\n");
    fprintf(out, "  \"loaders\": [\n");
    __block bool needComma = false;
    for ( uint32_t i = 0; i < loadersArrayCount; ++i ) {
        if ( needComma )
            fprintf(out, ",\n");
        atIndex(i)->print(state, out, printComments);
        needComma = true;
    }
    fprintf(out, "  ]");

    if ( this->mustBeMissingPathsCount > 0 ) {
        fprintf(out, ",\n  \"must-be-missing\": [\n");
        needComma = false;
        this->forEachMustBeMissingPath(^(const char* path, bool& stop) {
            if ( needComma )
                fprintf(out, ",\n");
            fprintf(out, "        \"%s\"", path);
            needComma = true;
        });
        fprintf(out, "\n    ]");
    }

    if ( this->cachePatchCount > 0 ) {
        fprintf(out, ",\n  \"cache-overrides\": [\n");
        needComma = false;
        this->forEachCachePatch(^(const CachePatch& patch) {
            if ( needComma )
                fprintf(out, ",\n");
            fprintf(out, "     {\n");
            fprintf(out, "        \"cache-dylib\":     \"%d\",\n", patch.cacheDylibIndex);
            fprintf(out, "        \"dylib-offset\":    \"0x%08X\",\n", patch.cacheDylibVMOffset);
            fprintf(out, "        \"replace-loader\":  \"%c.%d\",\n", patch.patchTo.loaderRef().app ? 'a' : 'c', patch.patchTo.loaderRef().index);
            fprintf(out, "        \"replace-offset\":  \"0x%08llX\"\n", patch.patchTo.offset());
            fprintf(out, "     }");
            needComma = true;
        });
        fprintf(out, "\n  ]");
    }

    // app specific ObjC selectors
    if ( const ObjCSelectorOpt* selOpt = this->objcSelectorOpt() ) {
        fprintf(out, ",\n  \"selector-table\": [");
        needComma = false;

        selOpt->forEachString(^(const PrebuiltLoader::BindTargetRef& target) {
            const Loader::LoaderRef& ref = target.loaderRef();
            if ( needComma )
                fprintf(out, ",");
            fprintf(out, "\n      {\n");
            fprintf(out, "          \"loader\":   \"%c.%d\",\n",
                    ref.app ? 'a' : 'c', ref.index);
            fprintf(out, "          \"offset\":   \"0x%08llX\"\n", target.offset());
            fprintf(out, "      }");
            needComma = true;
        });
        fprintf(out, "\n  ]");
    }

    // Objc classes
    if ( const ObjCClassOpt* clsOpt = this->objcClassOpt() ) {
        fprintf(out, ",\n  \"objc-class-table\": [");
        needComma = false;

        clsOpt->forEachClass(state, ^(const PrebuiltLoader::BindTargetRef& nameTarget,
                                      const Array<PrebuiltLoader::BindTargetRef>& implTargets) {
            const Loader::LoaderRef& nameRef = nameTarget.loaderRef();
            if ( needComma )
                fprintf(out, ",");
            fprintf(out, "\n      {\n");
            fprintf(out, "          \"name-loader\":   \"%c.%d\",\n",
                    nameRef.app ? 'a' : 'c', nameRef.index);
            fprintf(out, "          \"name-offset\":   \"0x%08llX\",\n", nameTarget.offset());

            if ( implTargets.count() == 1 ) {
                const PrebuiltLoader::BindTargetRef& implTarget = implTargets[0];
                const Loader::LoaderRef& implRef = implTarget.loaderRef();
                fprintf(out, "          \"impl-loader\":   \"%c.%d\",\n",
                        implRef.app ? 'a' : 'c', implRef.index);
                fprintf(out, "          \"impl-offset\":   \"0x%08llX\"\n", implTarget.offset());
            } else {
                bool needImplComma = false;
                for ( const PrebuiltLoader::BindTargetRef& implTarget : implTargets ) {
                    if ( needImplComma )
                        fprintf(out, ",\n");

                    const Loader::LoaderRef& ref = implTarget.loaderRef();
                    fprintf(out, "          \"impl-loader\":   \"%c.%d\",\n",
                            ref.app ? 'a' : 'c', ref.index);
                    fprintf(out, "          \"impl-offset\":   \"0x%08llX\"", implTarget.offset());

                    needImplComma = true;
                }
            }
            fprintf(out, "\n");
            fprintf(out, "      }");
            needComma = true;
        });
        fprintf(out, "\n  ]");
    }

    // Objc protocols
    if ( const ObjCClassOpt* protocolOpt = this->objcProtocolOpt() ) {
        fprintf(out, ",\n  \"objc-protocol-table\": [");
        needComma = false;

        protocolOpt->forEachClass(state, ^(const PrebuiltLoader::BindTargetRef& nameTarget,
                                           const Array<PrebuiltLoader::BindTargetRef>& implTargets) {
            const Loader::LoaderRef& nameRef = nameTarget.loaderRef();
            if ( needComma )
                fprintf(out, ",");
            fprintf(out, "\n      {\n");
            fprintf(out, "          \"name-loader\":   \"%c.%d\",\n",
                    nameRef.app ? 'a' : 'c', nameRef.index);
            fprintf(out, "          \"name-offset\":   \"0x%08llX\",\n", nameTarget.offset());
            if ( implTargets.count() == 1 ) {
                const PrebuiltLoader::BindTargetRef& implTarget = implTargets[0];
                const Loader::LoaderRef& implRef = implTarget.loaderRef();
                fprintf(out, "          \"impl-loader\":   \"%c.%d\",\n",
                        implRef.app ? 'a' : 'c', implRef.index);
                fprintf(out, "          \"impl-offset\":   \"0x%08llX\"\n", implTarget.offset());
            } else {
                bool needImplComma = false;
                for ( const PrebuiltLoader::BindTargetRef& implTarget : implTargets ) {
                    if ( needImplComma )
                        fprintf(out, ",\n");

                    const Loader::LoaderRef& ref = implTarget.loaderRef();
                    fprintf(out, "          \"impl-loader\":   \"%c.%d\",\n",
                            ref.app ? 'a' : 'c', ref.index);
                    fprintf(out, "          \"impl-offset\":   \"0x%08llX\"", implTarget.offset());

                    needImplComma = true;
                }
                fprintf(out, "\n");
            }
            fprintf(out, "      }");
            needComma = true;
        });
        fprintf(out, "\n  ]");
    }

    fprintf(out, "\n}\n");
}

const PrebuiltLoaderSet* PrebuiltLoaderSet::makeLaunchSet(Diagnostics& diag, RuntimeState& state, const MissingPaths& mustBeMissingPaths)
{
#if BUILDING_DYLD
    if ( !state.interposingTuplesAll.empty() ) {
        diag.error("cannot make PrebuiltLoaderSet for program that uses interposing");
        return nullptr;
    }
#elif BUILDING_CACHE_BUILDER
    // only dyld tries to populate state.interposingTuples, so in cache builder need to check for interposing in non-cached dylibs
    for ( const Loader* ldr : state.loaded ) {
        if ( ldr->dylibInDyldCache )
            break;
        const MachOAnalyzer* ma = ldr->analyzer(state);
        if ( ma->isDylib() && ma->hasInterposingTuples() ) {
            diag.error("cannot make PrebuiltLoaderSet for program that using interposing");
            return nullptr;
        }
    }
#endif
    if ( state.config.pathOverrides.dontUsePrebuiltForApp() ) {
        diag.error("cannot make PrebuiltLoaderSet for program that uses DYLD_* env vars");
        return nullptr;
    }
    if ( state.hasMissingFlatLazySymbols() ) {
        diag.error("cannot make PrebuiltLoaderSet for program that has missing flat lazy symbols");
        return nullptr;
    }

    // A launch may have JustInTimeLoaders at the top of the graph and PrebuiltLoaders at the bottom
    // The PrebuiltLoaders (from the dyld cache) may be re-used, so just make list of JIT ones
    STACK_ALLOC_ARRAY(JustInTimeLoader*, jitLoaders, state.loaded.size());
    uint16_t indexAsPrebuilt = 0;
    for ( const Loader* l : state.loaded ) {
        if ( JustInTimeLoader* jl = (JustInTimeLoader*)(l->isJustInTimeLoader()) ) {
            if ( jl->dylibInDyldCache ) {
                diag.error("cannot make PrebuiltLoader for dylib that is in dyld cache (%s)", jl->path());
                return nullptr;
            }
           if ( jl->isOverrideOfCachedDylib() ) {
                diag.error("cannot make PrebuiltLoader for dylib that overrides dylib in dyld cache (%s)", jl->path());
                return nullptr;
            }
            jitLoaders.push_back(jl);
            jl->ref.app   = true;
            jl->ref.index = indexAsPrebuilt++;
        }
    }

    // build objc since we are going to save this for next time
    PrebuiltObjC prebuiltObjC;
    {
        Diagnostics objcDiag;
        prebuiltObjC.make(objcDiag, state);
        // We deliberately disgard the diagnostic object as we can run without objc
        //TODO: Tell the user why their objc prevents faster launches
    }

    // initialize header of PrebuiltLoaderSet
    const uintptr_t count = jitLoaders.count();
    __block BumpAllocator   allocator;
    allocator.zeroFill(sizeof(PrebuiltLoaderSet));
    BumpAllocatorPtr<PrebuiltLoaderSet> set(allocator, 0);
    set->magic               = kMagic;
    set->versionHash         = PREBUILTLOADER_VERSION;
    set->loadersArrayCount   = (uint32_t)count;
    set->loadersArrayOffset  = sizeof(PrebuiltLoaderSet);
    set->cachePatchCount     = 0;
    set->cachePatchOffset    = 0;
    set->dyldCacheUUIDOffset = 0;
    set->objcSelectorHashTableOffset    = 0;
    set->objcClassHashTableOffset       = 0;
    set->objcProtocolHashTableOffset    = 0;
    set->objcProtocolClassCacheOffset   = 0;

    // initialize array of Loader offsets to zero
    allocator.zeroFill(count * sizeof(uint32_t));

#if BUILDING_DYLD
    // save UUID of dyld cache these PrebuiltLoaders were made against
    if ( const DyldSharedCache* cache = state.config.dyldCache.addr ) {
        set->dyldCacheUUIDOffset = (uint32_t)allocator.size();
        uuid_t uuid;
        cache->getUUID(uuid);
        allocator.append(uuid, sizeof(uuid_t));
    }
#endif

    // use block to save up all cache patches found while binding rest of PrebuiltClosureSet
    STACK_ALLOC_OVERFLOW_SAFE_ARRAY(CachePatch, cachePatches, 16);
    Loader::CacheWeakDefOverride cacheWeakDefFixup = ^(uint32_t cachedDylibIndex, uint32_t cachedDylibVMOffset, const Loader::ResolvedSymbol& target) {
        //state.log("patch index=%d, cacheOffset=0x%08x, symbol=%s, targetLoader=%s\n", cachedDylibIndex, exportCacheOffset, target.targetSymbolName, target.targetLoader->leafName());
        CachePatch patch = { cachedDylibIndex, cachedDylibVMOffset, PrebuiltLoader::BindTargetRef(target) };
        cachePatches.push_back(patch);
    };

    // serialize and append each image to PrebuiltLoaderSet
    for ( uintptr_t i = 0; i < count; ++i ) {
        uint32_t* loadersOffsetsAray = (uint32_t*)((uint8_t*)set.get() + set->loadersArrayOffset);
        loadersOffsetsAray[i]        = (uint32_t)allocator.size();
        Loader::LoaderRef buildingRef(true, i);
        PrebuiltLoader::serialize(diag, state, state.config.dyldCache.addr, *jitLoaders[i], buildingRef, cacheWeakDefFixup, prebuiltObjC, allocator);
        if ( diag.hasError() )
            return nullptr;
    }

    // Add objc if we have it
    if ( prebuiltObjC.builtObjC ) {
        // Selector hash table
        if ( !prebuiltObjC.selectorsHashTable.empty() ) {
            set->objcSelectorHashTableOffset = (uint32_t)allocator.size();
            allocator.append(prebuiltObjC.selectorsHashTable.begin(), prebuiltObjC.selectorsHashTable.count());
            allocator.align(8);
        }
        // Classes hash table
        if ( !prebuiltObjC.classesHashTable.empty() ) {
            set->objcClassHashTableOffset = (uint32_t)allocator.size();
            allocator.append(prebuiltObjC.classesHashTable.begin(), prebuiltObjC.classesHashTable.count());
            allocator.align(8);
        }
        // Protocols hash table
        if ( !prebuiltObjC.protocolsHashTable.empty() ) {
            set->objcProtocolHashTableOffset = (uint32_t)allocator.size();
            allocator.append(prebuiltObjC.protocolsHashTable.begin(), prebuiltObjC.protocolsHashTable.count());
            allocator.align(8);
        }
        set->objcProtocolClassCacheOffset = prebuiltObjC.objcProtocolClassCacheOffset;
    }

    // add cache patches to end
    if ( !cachePatches.empty() ) {
        set->cachePatchOffset = (uint32_t)allocator.size();
        for ( const CachePatch& patch : cachePatches ) {
            allocator.append(&patch, sizeof(patch));
            set->cachePatchCount += 1;
        }
    }

    // add must-be-missing paths to end
    if ( mustBeMissingPaths.size() != 0 ) {
        set->mustBeMissingPathsOffset = (uint32_t)allocator.size();
        mustBeMissingPaths.forEachPath(^(const char* path) {
            allocator.append(path, strlen(path)+1);
            set->mustBeMissingPathsCount += 1;
        });
    }

    // record final length
    set->length = (uint32_t)allocator.size();

    PrebuiltLoaderSet* result = (PrebuiltLoaderSet*)allocator.finalize();
    // result->print(stdout);
    return result;
}

void PrebuiltLoaderSet::forEachCachePatch(void (^handler)(const CachePatch&)) const
{
    const CachePatch* patchArray = (CachePatch*)((uint8_t*)this + this->cachePatchOffset);
    for ( uint32_t i = 0; i < this->cachePatchCount; ++i ) {
        handler(patchArray[i]);
    }
}

void PrebuiltLoaderSet::deallocate() const
{
    uintptr_t used = round_page(this->size());
    ::vm_deallocate(mach_task_self(), (long)this, used);
}

#if BUILDING_CACHE_BUILDER
const PrebuiltLoaderSet* PrebuiltLoaderSet::makeDyldCachePrebuiltLoaders(Diagnostics& diag, RuntimeState& state, const DyldSharedCache* dyldCacheInProgress, const Array<const Loader*>& jitLoaders)
{
    // scan JITLoaders and assign them prebuilt slots
    uint16_t indexAsPrebuilt = 0;
    for ( const Loader* ldr : jitLoaders ) {
        if ( ldr->isPrebuilt ) {
            diag.error("unexpected prebuilt loader in cached dylibs (%s)", ldr->path());
            return nullptr;
        }
        JustInTimeLoader* jldr = (JustInTimeLoader*)ldr;
        jldr->ref.app          = false;
        jldr->ref.index        = indexAsPrebuilt++;
    }

    // initialize header of PrebuiltLoaderSet
    const uintptr_t count = jitLoaders.count();
    BumpAllocator   allocator;
    allocator.zeroFill(sizeof(PrebuiltLoaderSet));
    BumpAllocatorPtr<PrebuiltLoaderSet> set(allocator, 0);
    set->magic               = kMagic;
    set->versionHash         = PREBUILTLOADER_VERSION;
    set->loadersArrayCount   = (uint32_t)count;
    set->loadersArrayOffset  = sizeof(PrebuiltLoaderSet);
    set->cachePatchCount     = 0;
    set->cachePatchOffset    = 0;
    set->dyldCacheUUIDOffset = 0;
    // initialize array of Loader offsets to zero
    allocator.zeroFill(count * sizeof(uint32_t));

    // serialize and append each image to PrebuiltLoaderSet
    for ( uintptr_t i = 0; i < count; ++i ) {
        BumpAllocatorPtr<uint32_t> loadersOffsetsArray(allocator, set->loadersArrayOffset);
        loadersOffsetsArray.get()[i] = (uint32_t)allocator.size();
        Loader::LoaderRef buildingRef(false, i);
        PrebuiltObjC prebuiltObjC;
        PrebuiltLoader::serialize(diag, state, dyldCacheInProgress, *((JustInTimeLoader*)jitLoaders[i]), buildingRef, nullptr, prebuiltObjC, allocator);
        if ( diag.hasError() )
            return nullptr;
    }

    set->length = (uint32_t)allocator.size();

    PrebuiltLoaderSet* result = (PrebuiltLoaderSet*)allocator.finalize();
    //    result->print();
    return result;
}
#endif


//
// MARK: --- BumpAllocator methods ---
//

void BumpAllocator::append(const void* payload, size_t payloadSize)
{
    size_t startSize = size();
    zeroFill(payloadSize);
    uint8_t* p = _vmAllocationStart + startSize;
    memcpy(p, payload, payloadSize);
}

void BumpAllocator::zeroFill(size_t reqSize)
{
    const size_t allocationChunk = 1024*1024;
    size_t remaining = _vmAllocationSize - this->size();
    if ( reqSize > remaining ) {
        // if current buffer too small, grow it
        size_t growth = _vmAllocationSize;
        if ( growth < allocationChunk )
            growth = allocationChunk;
        if ( growth < reqSize )
            growth = allocationChunk * ((reqSize / allocationChunk) + 1);
        vm_address_t newAllocationAddr;
        size_t       newAllocationSize = _vmAllocationSize + growth;
        ::vm_allocate(mach_task_self(), &newAllocationAddr, newAllocationSize, VM_FLAGS_ANYWHERE | VM_MAKE_TAG(VM_MEMORY_DYLD));
        assert(newAllocationAddr != 0);
        size_t currentInUse = this->size();
        if ( _vmAllocationStart != nullptr ) {
            ::memcpy((void*)newAllocationAddr, _vmAllocationStart, currentInUse);
            ::vm_deallocate(mach_task_self(), (vm_address_t)_vmAllocationStart, _vmAllocationSize);
        }
        _usageEnd          = (uint8_t*)(newAllocationAddr + currentInUse);
        _vmAllocationStart = (uint8_t*)newAllocationAddr;
        _vmAllocationSize  = newAllocationSize;
    }
    assert((uint8_t*)_usageEnd + reqSize <= (uint8_t*)_vmAllocationStart + _vmAllocationSize);
    _usageEnd += reqSize;
}

void BumpAllocator::align(unsigned multipleOf)
{
    size_t extra = size() % multipleOf;
    if ( extra == 0 )
        return;
    zeroFill(multipleOf - extra);
}

// truncates buffer to size used, makes it read-only, then returns pointer and clears BumpAllocator fields
const void* BumpAllocator::finalize()
{
    // trim vm allocation down to just what is needed
    uintptr_t bufferStart = (uintptr_t)_vmAllocationStart;
    uintptr_t used        = round_page(this->size());
    if ( used < _vmAllocationSize ) {
        uintptr_t deallocStart = bufferStart + used;
        ::vm_deallocate(mach_task_self(), deallocStart, _vmAllocationSize - used);
        _usageEnd         = nullptr;
        _vmAllocationSize = used;
    }
    // mark vm region read-only
    ::vm_protect(mach_task_self(), bufferStart, used, false, VM_PROT_READ);
    _vmAllocationStart = nullptr;
    return (void*)bufferStart;
}

BumpAllocator::~BumpAllocator()
{
    if ( _vmAllocationStart != nullptr ) {
        ::vm_deallocate(mach_task_self(), (vm_address_t)_vmAllocationStart, _vmAllocationSize);
        _vmAllocationStart  = nullptr;
        _vmAllocationSize   = 0;
        _usageEnd           = nullptr;
    }
}


//
// MARK: --- MissingPaths methods ---
//

void MissingPaths::addPath(const char* path)
{
    this->append(path, strlen(path)+1);
}

void MissingPaths::forEachPath(void (^callback)(const char* path)) const
{
    for (const uint8_t* s = _vmAllocationStart; s < _usageEnd; ++s) {
        const char* str = (char*)s;
        callback(str);
        s += strlen(str);
    }
}


} // namespace dyld4
