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

#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <mach-o/nlist.h>
#include <TargetConditionals.h>

#include "MachOAnalyzer.h"
#include "Loader.h"
#include "JustInTimeLoader.h"
#include "PrebuiltLoader.h"
#include "DyldRuntimeState.h"
#include "DyldProcessConfig.h"
#include "StringUtils.h"
#include "DebuggerSupport.h"
#include "RosettaSupport.h"
#include "Tracing.h"

using dyld3::MachOAnalyzer;
using dyld3::MachOFile;

namespace dyld4 {

Loader::InitialOptions::InitialOptions()
    : inDyldCache(false)
    , hasObjc(false)
    , mayHavePlusLoad(false)
    , roData(false)
    , neverUnloaded(false)
    , leaveMapped(false)
{
}

Loader::InitialOptions::InitialOptions(const Loader& other)
    : inDyldCache(other.dylibInDyldCache)
    , hasObjc(other.hasObjC)
    , mayHavePlusLoad(other.mayHavePlusLoad)
    , roData(other.hasReadOnlyData)
    , neverUnloaded(other.neverUnload)
    , leaveMapped(other.leaveMapped)
{
}

const char* Loader::path() const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->path();
    else
        return ((JustInTimeLoader*)this)->path();
}

const MachOLoaded* Loader::loadAddress(RuntimeState& state) const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->loadAddress(state);
    else
        return ((JustInTimeLoader*)this)->loadAddress(state);
}

bool Loader::contains(RuntimeState& state, const void* addr, const void** segAddr, uint64_t* segSize, uint8_t* segPerms) const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->contains(state, addr, segAddr, segSize, segPerms);
    else
        return ((JustInTimeLoader*)this)->contains(state, addr, segAddr, segSize, segPerms);
}

bool Loader::matchesPath(const char* path) const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->matchesPath(path);
    else
        return ((JustInTimeLoader*)this)->matchesPath(path);
}

FileID Loader::fileID() const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->fileID();
    else
        return ((JustInTimeLoader*)this)->fileID();
}

uint32_t Loader::dependentCount() const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->dependentCount();
    else
        return ((JustInTimeLoader*)this)->dependentCount();
}

Loader* Loader::dependent(const RuntimeState& state, uint32_t depIndex, DependentKind* kind) const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->dependent(state, depIndex, kind);
    else
        return ((JustInTimeLoader*)this)->dependent(state, depIndex, kind);
}

void Loader::loadDependents(Diagnostics& diag, RuntimeState& state, const LoadOptions& options)
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->loadDependents(diag, state, options);
    else
        return ((JustInTimeLoader*)this)->loadDependents(diag, state, options);
}

bool Loader::getExportsTrie(uint64_t& runtimeOffset, uint32_t& size) const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->getExportsTrie(runtimeOffset, size);
    else
        return ((JustInTimeLoader*)this)->getExportsTrie(runtimeOffset, size);
}

bool Loader::hiddenFromFlat(bool forceGlobal) const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->hiddenFromFlat(forceGlobal);
    else
        return ((JustInTimeLoader*)this)->hiddenFromFlat(forceGlobal);
}

bool Loader::representsCachedDylibIndex(uint16_t dylibIndex) const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->representsCachedDylibIndex(dylibIndex);
    else
        return ((JustInTimeLoader*)this)->representsCachedDylibIndex(dylibIndex);
}

void Loader::applyFixups(Diagnostics& diag, RuntimeState& state, DyldCacheDataConstLazyScopedWriter& dataConst, bool allowLazyBinds) const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->applyFixups(diag, state, dataConst, allowLazyBinds);
    else
        return ((JustInTimeLoader*)this)->applyFixups(diag, state, dataConst, allowLazyBinds);
}

bool Loader::overridesDylibInCache(const DylibPatch*& patchTable, uint16_t& cacheDylibOverriddenIndex) const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->overridesDylibInCache(patchTable, cacheDylibOverriddenIndex);
    else
        return ((JustInTimeLoader*)this)->overridesDylibInCache(patchTable, cacheDylibOverriddenIndex);
}

void Loader::unmap(RuntimeState& state, bool force) const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->unmap(state, force);
    else
        return ((JustInTimeLoader*)this)->unmap(state, force);
}


bool Loader::hasBeenFixedUp(RuntimeState& state) const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->hasBeenFixedUp(state);
    else
        return ((JustInTimeLoader*)this)->hasBeenFixedUp(state);
}


bool Loader::beginInitializers(RuntimeState& state)
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        return ((PrebuiltLoader*)this)->beginInitializers(state);
    else
        return ((JustInTimeLoader*)this)->beginInitializers(state);
}

void Loader::runInitializers(RuntimeState& state) const
{
    assert(this->magic == kMagic);
    if ( this->isPrebuilt )
        ((PrebuiltLoader*)this)->runInitializers(state);
    else
        ((JustInTimeLoader*)this)->runInitializers(state);
}

const PrebuiltLoader* Loader::LoaderRef::loader(const RuntimeState& state) const
{
    if ( this->app )
        return state.processPrebuiltLoaderSet()->atIndex(this->index);
    else
        return state.cachedDylibsPrebuiltLoaderSet()->atIndex(this->index);
}

const char* Loader::leafName(const char* path)
{
    if ( const char* lastSlash = strrchr(path, '/') )
        return lastSlash + 1;
    else
        return path;
}

const char* Loader::leafName() const
{
    return leafName(path());
}

bool Loader::hasMagic() const
{
    return (this->magic == kMagic);
}

void Loader::appendHexNibble(uint8_t value, char*& p)
{
    if ( value < 10 )
        *p++ = '0' + value;
    else
        *p++ = 'A' + value - 10;
}

void Loader::appendHexByte(uint8_t value, char*& p)
{
    value &= 0xFF;
    appendHexNibble(value >> 4, p);
    appendHexNibble(value & 0x0F, p);
}

void Loader::uuidToStr(uuid_t uuid, char  uuidStr[64])
{
    char* p = uuidStr;
    appendHexByte(uuid[0], p);
    appendHexByte(uuid[1], p);
    appendHexByte(uuid[2], p);
    appendHexByte(uuid[3], p);
    *p++ = '-';
    appendHexByte(uuid[4], p);
    appendHexByte(uuid[5], p);
    *p++ = '-';
    appendHexByte(uuid[6], p);
    appendHexByte(uuid[7], p);
    *p++ = '-';
    appendHexByte(uuid[8], p);
    appendHexByte(uuid[9], p);
    *p++ = '-';
    appendHexByte(uuid[10], p);
    appendHexByte(uuid[11], p);
    appendHexByte(uuid[12], p);
    appendHexByte(uuid[13], p);
    appendHexByte(uuid[14], p);
    appendHexByte(uuid[15], p);
    *p = '\0';
}

void Loader::logLoad(RuntimeState& state, const MachOLoaded* ml, const char* path)
{
    uuid_t uuid;
    if ( ml->getUuid(uuid) ) {
        char  uuidStr[64];
        uuidToStr(uuid, uuidStr);
        state.log("<%s> %s\n", uuidStr, path);
    }
    else {
        state.log("<no uuid> %s\n", path);
    }
}

const Loader* Loader::makeDiskLoader(Diagnostics& diag, RuntimeState& state, const char* path, const LoadOptions& options, bool overridesDyldCache, uint32_t dylibIndex)
{
    // never create a new loader in RTLD_NOLOAD mode
    if ( options.rtldNoLoad )
        return nullptr;

    // don't use PrebuiltLoaders for simulator because the paths will be wrong (missing SIMROOT prefix)
#if !TARGET_OS_SIMULATOR
    // first check for a PrebuiltLoader
    const Loader* result = (Loader*)state.findPrebuiltLoader(path);
    if ( result != nullptr )
        return result;
#endif

    // try building a JustInTime Loader
    return JustInTimeLoader::makeJustInTimeLoaderDisk(diag, state, path, options, overridesDyldCache, dylibIndex);
}

const Loader* Loader::makeDyldCacheLoader(Diagnostics& diag, RuntimeState& state, const char* path, const LoadOptions& options, uint32_t dylibIndex)
{
    // never create a new loader in RTLD_NOLOAD mode
    if ( options.rtldNoLoad )
        return nullptr;

#if !TARGET_OS_SIMULATOR
    // first check for a PrebuiltLoader with compatible platform
    // rdar://76406035 (simulator cache paths need prefix)
    const Loader* result = (Loader*)state.findPrebuiltLoader(path);
    if ( result != nullptr ) {
        if ( result->loadAddress(state)->loadableIntoProcess(state.config.process.platform, path) )
            return result;
    }
#endif

    // try building a JustInTime Loader
    return JustInTimeLoader::makeJustInTimeLoaderDyldCache(diag, state, path, options, dylibIndex);
}

static bool isFileRelativePath(const char* path)
{
    if ( path[0] == '/' )
        return false;
    if ( (path[0] == '.') && (path[1] == '/') )
        return true;
    if ( (path[0] == '.') && (path[1] == '.') && (path[2] == '/') )
        return true;
    return (path[0] != '@');
}

// This composes DyldProcessConfig::forEachPathVariant() with Loader::forEachResolvedAtPathVar()
// They are separate layers because DyldProcessConfig handles DYLD_ env vars and Loader handle @ paths
void Loader::forEachPath(Diagnostics& diag, RuntimeState& state, const char* loadPath, const LoadOptions& options,
                         void (^handler)(const char* possiblePath, ProcessConfig::PathOverrides::Type type, bool& stop))
{
    __block bool stop = false;
    const ProcessConfig::PathOverrides& po = state.config.pathOverrides;
    po.forEachPathVariant(loadPath, state.config.process.platform, !options.useFallBackPaths, stop,
                          ^(const char* possibleVariantPath, ProcessConfig::PathOverrides::Type type, bool&) {
                                // passing a leaf name to dlopen() allows rpath searching for it
                                if ( !options.staticLinkage && (possibleVariantPath == loadPath) && (loadPath[0] != '@') && (loadPath[0] != '/') ) {
                                    char implicitRPath[512];
                                    strcpy(implicitRPath, "@rpath/");
                                    strlcat(implicitRPath, possibleVariantPath, sizeof(implicitRPath));
                                    Loader::forEachResolvedAtPathVar(state, implicitRPath, options, ProcessConfig::PathOverrides::Type::implictRpathExpansion, stop, handler);
                                 }
                                 if ( stop )
                                    return;

                                // expand @ paths
                                Loader::forEachResolvedAtPathVar(state, possibleVariantPath, options, type, stop, handler);
                          });
}

//
// Use PathOverrides class to walk possible paths, for each, look on disk, then in cache.
// Special case customer caches to look in cache first, to avoid stat() when result will be disgarded.
// For dylibs loaded from disk, we need to know if they override something in the cache in order to patch it in.
// It is considered an override if the initial path or path found is in the dyld cache
//
const Loader* Loader::getLoader(Diagnostics& diag, RuntimeState& state, const char* loadPath, const LoadOptions& options)
{
    __block const Loader*  result        = nullptr;
    const DyldSharedCache* cache         = state.config.dyldCache.addr;
    const bool             customerCache = (cache != nullptr) && (cache->header.cacheType == kDyldSharedCacheTypeProduction);

    if ( state.config.log.searching )
        state.log("find path \"%s\"\n", loadPath);

    const bool loadPathIsRPath            = (::strncmp(loadPath, "@rpath/", 7) == 0);
    const bool loadPathIsFileRelativePath = isFileRelativePath(loadPath);

    // for @rpath paths, first check if already loaded as rpath
    if ( loadPathIsRPath ) {
        for ( const Loader* ldr : state.loaded ) {
            if ( ldr->matchesPath(loadPath) ) {
                if ( state.config.log.searching )
                    state.log("  found: already-loaded-by-rpath: %s\n", ldr->path());
                return ldr;
            }
        }
    }
    else if ( !options.staticLinkage && (loadPath[0] != '@') && (loadPath[0] != '/') && (strchr(loadPath, '/') == nullptr) ) {
        // handle dlopen("xxx") to mean "@rpath/xxx" when it is already loaded
        char implicitRPath[strlen(loadPath)+8];
        strlcpy(implicitRPath, "@rpath/", sizeof(implicitRPath));
        strlcat(implicitRPath, loadPath, sizeof(implicitRPath));
        for ( const Loader* ldr : state.loaded ) {
            if ( ldr->matchesPath(implicitRPath) ) {
                if ( state.config.log.searching )
                    state.log("  found: already-loaded-by-rpath: %s\n", ldr->path());
                return ldr;
            }
        }
    }

    // canonicalize shared cache paths
    if ( const char* canonicalPathInCache = state.config.canonicalDylibPathInCache(loadPath) ) {
        if ( strcmp(canonicalPathInCache, loadPath) != 0 ) {
            loadPath = canonicalPathInCache;
            if ( state.config.log.searching )
                state.log("  switch to canonical cache path: %s\n", loadPath);
        }
    }

    // get info about original path
    __block uint32_t dylibInCacheIndex;
    const bool       originalPathIsInDyldCache            = state.config.dyldCache.indexOfPath(loadPath, dylibInCacheIndex);
    const bool       originalPathIsOverridableInDyldCache = originalPathIsInDyldCache && cache->isOverridablePath(loadPath);

    // search all locations
    Loader::forEachPath(diag, state, loadPath, options,
                        ^(const char* possiblePath, ProcessConfig::PathOverrides::Type type, bool& stop) {
                            // On customer dyld caches, if loaded a path in cache, don't look for overrides
                            if ( customerCache && originalPathIsInDyldCache && !originalPathIsOverridableInDyldCache && (possiblePath != loadPath) )
                                return;
                            if ( state.config.log.searching )
                                state.log("  possible path(%s): \"%s\"\n", ProcessConfig::PathOverrides::typeName(type), possiblePath);

                            // check if this path already in use by a Loader
                            for ( const Loader* ldr : state.loaded ) {
                                if ( ldr->matchesPath(possiblePath) ) {
                                    result = ldr;
                                    stop   = true;
                                    diag.clearError(); // found dylib, so clear any errors from previous paths tried
                                    if ( state.config.log.searching )
                                        state.log("  found: already-loaded-by-path: \"%s\"\n", possiblePath);
                                    return;
                                }
                            }

                            // <rdar://problem/47682983> don't allow file system relative paths in hardened programs
                            // (type == ProcessConfig::PathOverrides::Type::implictRpathExpansion)
                            if ( !state.config.security.allowEnvVarsPath && isFileRelativePath(possiblePath) ) {
                                if ( diag.noError() )
                                    diag.error("tried: '%s' (relative path not allowed in hardened program)", possiblePath);
                                else
                                    diag.appendError(", '%s' (relative path not allowed in hardened program)", possiblePath);
                                return;
                            }

                            // check dyld cache trie to see if this is an alias to a cached dylib
                            uint32_t possibleCacheIndex;
                            if ( state.config.dyldCache.indexOfPath(possiblePath, possibleCacheIndex) ) {
                                for ( const Loader* ldr : state.loaded ) {
                                    if ( ldr->representsCachedDylibIndex(possibleCacheIndex) ) {
                                        result = ldr;
                                        stop   = true;
                                        diag.clearError(); // found dylib, so clear any errors from previous paths tried
                                        if ( state.config.log.searching )
                                            state.log("  found: already-loaded-by-dylib-index: \"%s\" -> %s\n", possiblePath, ldr->path());
                                        return;
                                    }
                                }
                            }

                            // RTLD_NOLOAD used and this possible path not already in use, so skip to next
                            if ( options.rtldNoLoad ) {
                                return;
                            }

                            // see if this path is on disk or in dyld cache
                            bool   possiblePathHasFileOnDisk  = false;
                            bool   possiblePathIsInDyldCache  = false;
                            bool   possiblePathOverridesCache = false;
                            bool   notAFile                   = false;
                            FileID possiblePathFileID = FileID::none();
                            if ( customerCache ) {
                                // for customer cache, check cache first and only stat() if overridable
                                possiblePathIsInDyldCache = state.config.dyldCache.indexOfPath(possiblePath, dylibInCacheIndex);
                                if ( possiblePathIsInDyldCache ) {
                                    if ( cache->isOverridablePath(possiblePath) ) {
                                        // see if there is a root installed that overrides one of few overridable dylibs in the cache
                                        possiblePathHasFileOnDisk  = state.config.fileExists(possiblePath, &possiblePathFileID, &notAFile);
                                        possiblePathOverridesCache = possiblePathHasFileOnDisk;
                                    }
                                }
                                else {
                                    possiblePathHasFileOnDisk  = state.config.fileExists(possiblePath, &possiblePathFileID, &notAFile);
                                    possiblePathOverridesCache = possiblePathHasFileOnDisk && originalPathIsOverridableInDyldCache;
                                }
                            }
                            else {
                                // for dev caches, always stat() and check cache
                                possiblePathHasFileOnDisk  = state.config.fileExists(possiblePath, &possiblePathFileID, &notAFile);
                                possiblePathIsInDyldCache  = state.config.dyldCache.indexOfPath(possiblePath, dylibInCacheIndex);
                                possiblePathOverridesCache = possiblePathHasFileOnDisk && (originalPathIsInDyldCache || possiblePathIsInDyldCache);
                            }

                            // see if this possible path was already loaded via a symlink or hardlink by checking inode
                            if ( possiblePathHasFileOnDisk && possiblePathFileID.valid() ) {
                                for ( const Loader* ldr : state.loaded ) {
                                    FileID ldrFileID = ldr->fileID();
                                    if ( ldrFileID.valid() && (possiblePathFileID == ldrFileID) ) {
                                        result = ldr;
                                        stop   = true;
                                        diag.clearError(); // found dylib, so clear any errors from previous paths tried
                                        if ( state.config.log.searching )
                                            state.log("  found: already-loaded-by-inode-mtime: \"%s\"\n", ldr->path());
                                        return;
                                    }
                                }
                            }

#if TARGET_OS_SIMULATOR
                            // rdar://76406035 (load simulator dylibs from cache)
                            if ( (state.config.dyldCache.addr != nullptr) && state.config.dyldCache.addr->header.dylibsExpectedOnDisk ) {
                                if ( const char* simRoot = state.config.pathOverrides.simRootPath() ) {
                                    size_t simRootLen = strlen(simRoot);
                                    // compare inode/mtime of dylib now vs when cache was built
                                    const char* possiblePathInSimDyldCache = nullptr;
                                    if ( strncmp(possiblePath, simRoot, simRootLen) == 0 ) {
                                        // looks like a dylib in the sim Runtime root, see if partial path is in the dyld cache
                                        possiblePathInSimDyldCache = &possiblePath[simRootLen];
                                    }
                                    else if ( strncmp(possiblePath, "/usr/lib/system/", 16) == 0 ) {
                                        // could be one of the magic host dylibs that got incorporated into the dyld cache
                                        possiblePathInSimDyldCache = possiblePath;
                                    }
                                    if ( possiblePathInSimDyldCache != nullptr ) {
                                        if ( state.config.dyldCache.indexOfPath(possiblePathInSimDyldCache, dylibInCacheIndex) ) {
                                            uint64_t expectedMTime;
                                            uint64_t expectedInode;
                                            state.config.dyldCache.addr->getIndexedImageEntry(dylibInCacheIndex, expectedMTime, expectedInode);
                                            FileID expectedID(expectedInode, expectedMTime, true);
                                            if ( possiblePathFileID == expectedID ) {
                                                // inode/mtime matches when sim dyld cache was built, so use dylib from dyld cache and ignore file on disk
                                                possiblePathHasFileOnDisk = false;
                                                possiblePathIsInDyldCache = true;
                                            }
                                        }
                                    }
                                }
                            }
#endif
                            // if possiblePath not a file and not in dyld cache, skip to next possible path
                            if ( !possiblePathHasFileOnDisk && !possiblePathIsInDyldCache ) {
                                if ( options.pathNotFoundHandler )
                                    options.pathNotFoundHandler(possiblePath);
                                // set diag to be contain all errors from all paths tried
                                if ( diag.noError() ) {
                                    if ( notAFile )
                                        diag.error("tried: '%s' (not a file)", possiblePath);
                                    else
                                        diag.error("tried: '%s' (no such file)", possiblePath);
                                }
                                else {
                                    if ( notAFile )
                                        diag.appendError(", '%s' (not a file)", possiblePath);
                                    else
                                        diag.appendError(", '%s' (no such file)", possiblePath);
                                }
                                return;
                            }

                            // try to build Loader from possiblePath
                            Diagnostics possiblePathDiag;
                            if ( possiblePathHasFileOnDisk ) {
                                if ( possiblePathOverridesCache ) {
                                    // use dylib on disk to override dyld cache
                                    if ( state.config.log.searching )
                                        state.log("  found: dylib-from-disk-to-override-cache: \"%s\"\n", possiblePath);
                                    result = makeDiskLoader(possiblePathDiag, state, possiblePath, options, true, dylibInCacheIndex);
                                    if ( state.config.log.searching && possiblePathDiag.hasError() )
                                        state.log("  found: dylib-from-disk-to-override-cache-error: \"%s\" => \"%s\"\n", possiblePath, possiblePathDiag.errorMessageCStr());
                                }
                                else {
                                    // load from disk, nothing to do with dyld cache
                                    if ( state.config.log.searching )
                                        state.log("  found: dylib-from-disk: \"%s\"\n", possiblePath);
                                    result = makeDiskLoader(possiblePathDiag, state, possiblePath, options, false, 0);
                                    if ( state.config.log.searching && possiblePathDiag.hasError() )
                                        state.log("  found: dylib-from-disk-error: \"%s\" => \"%s\"\n", possiblePath, possiblePathDiag.errorMessageCStr());
                                }
                            }
                            else if ( possiblePathIsInDyldCache ) {
                                // can use dylib in dyld cache
                                if ( state.config.log.searching )
                                    state.log("  found: dylib-from-cache: (0x%04X) \"%s\"\n", dylibInCacheIndex, possiblePath);
                                result = makeDyldCacheLoader(possiblePathDiag, state, possiblePath, options, dylibInCacheIndex);
                                if ( state.config.log.searching && possiblePathDiag.hasError() )
                                    state.log("  found: dylib-from-cache-error: \"%s\" => \"%s\"\n", possiblePath, possiblePathDiag.errorMessageCStr());
                            }
                            if ( result != nullptr ) {
                                stop = true;
                                diag.clearError(); // found dylib, so clear any errors from previous paths tried
                            }
                            else {
                                // set diag to be contain all errors from all paths tried
                                if ( diag.noError() )
                                    diag.error("tried: '%s' (%s)", possiblePath, possiblePathDiag.errorMessageCStr());
                                else
                                    diag.appendError(", '%s' (%s)", possiblePath, possiblePathDiag.errorMessageCStr());
                            }
                        });

    // The last possibility is that the path provided has ../ or // in it,
    // or is a symlink to a dylib which is in the cache and no longer on disk.
    // Use realpath() and try getLoader() again.
    // Do this last and only if it would fail anyways so as to not slow down correct paths
    if ( result == nullptr ) {
        if ( !state.config.security.allowEnvVarsPath && loadPathIsFileRelativePath ) {
            // don't realpath() relative paths in hardened programs
        }
        else {
            char canonicalPath[PATH_MAX];
            if ( (loadPath[0] != '@') && state.config.syscall.realpath(loadPath, canonicalPath) ) {
                // only call getLoader() again if the realpath is different to prevent recursion
                // don't call getLoader() again if the realpath is a just the loadPath cut back, because that means some dir was not found
                if ( ::strncmp(loadPath, canonicalPath, strlen(canonicalPath)) != 0 ) {
                    if ( state.config.log.searching )
                        state.log("  switch to realpath: \"%s\"\n", canonicalPath);
                    result = getLoader(diag, state, canonicalPath, options);
                }
            }
        }
    }
    if ( state.config.log.searching && (result == nullptr) )
        state.log("  not found: \"%s\"\n", loadPath);

    // if the load failed due to security policy, leave a hint in dlerror() or crash log messages 
    if ( (result == nullptr) && (loadPath[0] == '@') && !state.config.security.allowAtPaths ) {
        diag.appendError(", (security policy does not allow @ path expansion)");
    }

    // if dylib could not be found, but is not required, clear error message
    if ( (result == nullptr) && (options.canBeMissing || options.rtldNoLoad) ) {
        diag.clearError();
    }
    return result;
}

bool Loader::expandAtLoaderPath(RuntimeState& state, const char* loadPath, const LoadOptions& options, const Loader* ldr, bool fromLCRPATH, char fixedPath[])
{
    // only do something if path starts with @loader_path
    if ( strncmp(loadPath, "@loader_path", 12) != 0 )
        return false;
    if ( (loadPath[12] != '/') && (loadPath[12] != '\0') )
        return false;

    // don't support @loader_path in DYLD_INSERT_LIBRARIES
    if ( options.insertedDylib ) {
        if ( state.config.log.searching )
            state.log("    @loader_path not allowed in DYLD_INSERT_LIBRARIES\n");
        return false;
    }

    // don't expand if security does not allow
    if ( !state.config.security.allowAtPaths && fromLCRPATH && (ldr == state.mainExecutableLoader) ) {
        // <rdar://42360708> but allow @loader_path in LC_LOAD_DYLIB during dlopen()
        if ( state.config.log.searching )
            state.log("    @loader_path in LC_RPATH from main executable not expanded due to security policy\n");
        return false;
    }

    strlcpy(fixedPath, ldr->path(), PATH_MAX);
    char* lastSlash = strrchr(fixedPath, '/');
    if ( lastSlash != nullptr ) {
        strcpy(lastSlash, &loadPath[12]);
        return true;
    }
    return false;
}

bool Loader::expandAtExecutablePath(RuntimeState& state, const char* loadPath, const LoadOptions& options, bool fromLCRPATH, char fixedPath[])
{
    // only do something if path starts with @executable_path
    if ( strncmp(loadPath, "@executable_path", 16) != 0 )
        return false;
    if ( (loadPath[16] != '/') && (loadPath[16] != '\0') )
        return false;

    // don't expand if security does not allow
    if ( !state.config.security.allowAtPaths ) {
        if ( state.config.log.searching )
            state.log("    @executable_path not expanded due to security policy\n");
        return false;
    }

    strlcpy(fixedPath, state.config.process.mainExecutablePath, PATH_MAX);
    char* lastSlash = strrchr(fixedPath, '/');
    if ( lastSlash != nullptr ) {
        if ( loadPath[16] == '/' )
            strcpy(lastSlash, &loadPath[16]);
        else
            strcpy(lastSlash + 1, &loadPath[16]);
        return true;
    }
    return false;
}

static void concatenatePaths(char *path, const char *suffix, size_t pathsize)
{
    if ( (path[strlen(path) - 1] == '/') && (suffix[0] == '/') )
        strlcat(path, &suffix[1], pathsize); // avoid double slash when combining path
    else
        strlcat(path, suffix, pathsize);
}

void Loader::forEachResolvedAtPathVar(RuntimeState& state, const char* loadPath, const LoadOptions& options, ProcessConfig::PathOverrides::Type type, bool& stop,
                                      void (^handler)(const char* possiblePath, ProcessConfig::PathOverrides::Type type, bool& stop))
{
    // don't expand @rpath in DYLD_INSERT_LIBRARIES
    bool isRPath = (strncmp(loadPath, "@rpath/", 7) == 0);
    if ( isRPath && options.insertedDylib ) {
        handler(loadPath, type, stop);
        return;
    }

    // expand @loader_path
    BLOCK_ACCCESSIBLE_ARRAY(char, tempPath, PATH_MAX);
    if ( expandAtLoaderPath(state, loadPath, options, options.rpathStack->image, false, tempPath) ) {
        handler(tempPath, ProcessConfig::PathOverrides::Type::loaderPathExpansion, stop);
#if BUILDING_DYLD && TARGET_OS_OSX
        if ( !stop ) {
            // using @loader_path, but what it expanded to did not work ('stop' not set)
            // maybe this is an old binary with an install name missing the /Versions/A/ part
            const Loader*         orgLoader   = options.rpathStack->image;
            const MachOAnalyzer*  orgMA       = orgLoader->analyzer(state);
            if ( orgMA->isDylib() && !orgMA->enforceFormat(MachOAnalyzer::Malformed::loaderPathsAreReal) ) {
                const char*           fullPath    = orgLoader->path();
                const char*           installPath = orgMA->installName();
                if ( const char* installLeaf = strrchr(installPath, '/') ) {
                    size_t leafLen = strlen(installLeaf);
                    size_t fullLen = strlen(fullPath);
                    if ( fullLen > (leafLen+11) ) {
                        const char* fullWhereVersionMayBe = &fullPath[fullLen-leafLen-11];
                        if ( strncmp(fullWhereVersionMayBe, "/Versions/", 10) == 0 ) {
                            // try expanding @loader_path to this framework's path that is missing /Versions/A part
                            strlcpy(tempPath, fullPath, PATH_MAX);
                            tempPath[fullLen-leafLen-11] = '\0';
                            strlcat(tempPath, &loadPath[12], PATH_MAX);
                            handler(tempPath, ProcessConfig::PathOverrides::Type::loaderPathExpansion, stop);
                        }
                    }
                }
            }
        }
#endif
        return;
    }

    // expand @executable_path
    if ( expandAtExecutablePath(state, loadPath, options, false, tempPath) ) {
        handler(tempPath, ProcessConfig::PathOverrides::Type::executablePathExpansion, stop);
        return;
    }

    // expand @rpath
    if ( isRPath ) {
        // note: rpathTail starts with '/'
        const char* rpathTail = &loadPath[6];
        // keep track if this is an explict @rpath or implicit
        ProcessConfig::PathOverrides::Type expandType = ProcessConfig::PathOverrides::Type::rpathExpansion;
        if ( type == ProcessConfig::PathOverrides::Type::implictRpathExpansion )
            expandType = type;
        // rpath is expansion is a stack of rpath dirs built starting with main executable and pushing
        // LC_RPATHS from each dylib as they are recursively loaded.  options.rpathStack is a linnked list of that stack.
        for ( const LoadChain* link = options.rpathStack; (link != nullptr) && !stop; link = link->previous ) {
            const MachOAnalyzer* ma = link->image->analyzer(state);
            ma->forEachRPath(^(const char* rPath, bool& innerStop) {
                if ( state.config.log.searching )
                    state.log("  LC_RPATH '%s' from '%s'\n", rPath, link->image->path());
                if ( expandAtLoaderPath(state, rPath, options, link->image, true, tempPath) || expandAtExecutablePath(state, rPath, options, true, tempPath) ) {
                    concatenatePaths(tempPath, rpathTail, PATH_MAX);
                    handler(tempPath, expandType, innerStop);
                }
                else if ( rPath[0] == '/' ) {
#if BUILDING_DYLD && TARGET_OS_OSX && __arm64__ // FIXME: this should be a runtime check to enable unit testing
                    // if LC_RPATH is to absolute path like /usr/lib/swift, but this iOS app running on macOS, we really need /System/iOSSupport/usr/lib/swift
                    if ( state.config.process.platform == dyld3::Platform::iOS ) {
                        strlcpy(tempPath, "/System/iOSSupport", PATH_MAX);
                        strlcat(tempPath, rPath, PATH_MAX);
                        concatenatePaths(tempPath, rpathTail, PATH_MAX);
                        handler(tempPath, expandType, innerStop);
                        if ( innerStop ) {
                            stop = true;
                            return;
                        }
                    }
                    // fall through
#endif
#if TARGET_OS_SIMULATOR
                    // <rdar://problem/5869973> DYLD_ROOT_PATH should apply to LC_RPATH rpaths
                    if ( const char* simRoot = state.config.pathOverrides.simRootPath() ) {
                        strlcpy(tempPath, simRoot, PATH_MAX);
                        strlcat(tempPath, rPath, PATH_MAX);
                        concatenatePaths(tempPath, rpathTail, PATH_MAX);
                        handler(tempPath, expandType, innerStop);
                        if ( innerStop ) {
                            stop = true;
                            return;
                        }
                    }
					// <rdar://problem/49576123> Even if DYLD_ROOT_PATH exists, LC_RPATH should add raw path to rpaths
                    // so fall through
#endif
                    // LC_RPATH is an absolute path, not blocked by AtPath::none
                    strlcpy(tempPath, rPath, PATH_MAX);
                    concatenatePaths(tempPath, rpathTail, PATH_MAX);
                    handler(tempPath, expandType, innerStop);
                } else {
#if BUILDING_DYLD && TARGET_OS_OSX // FIXME: this should be a runtime check to enable unit testing
                    // <rdar://81909581>
                    // Relative paths.  Only allow these if security supports them
                    if ( state.config.security.allowAtPaths ) {
                        strlcpy(tempPath, rPath, PATH_MAX);
                        concatenatePaths(tempPath, rpathTail, PATH_MAX);
                        handler(tempPath, expandType, innerStop);
                    }
#endif
                }
                if ( innerStop )
                    stop = true;
            });
        }
        if ( stop )
            return;
    }

    // only call with origin path if it did not start with @
    if ( loadPath[0] != '@' ) {
        handler(loadPath, type, stop);
    }
}

const Loader* Loader::alreadyLoaded(RuntimeState& state, const char* loadPath)
{
    FileID fileID = FileID::none();
    bool   fileExists = (loadPath[0] != '@') && state.config.fileExists(loadPath, &fileID);
    for ( const Loader* ldr : state.loaded ) {
        if ( ldr->matchesPath(loadPath) )
            return ldr;
        if ( fileExists && fileID.valid() ) {
            FileID ldrFileID = ldr->fileID();
            if ( ldrFileID.valid() && (fileID == ldrFileID) )
                return ldr;
        }
    }
    return nullptr;
}

uint64_t Loader::validateFile(Diagnostics& diag, const RuntimeState& state, int fd, const char* path,
                              const CodeSignatureInFile& codeSignature, const Loader::FileValidationInfo& fileValidation)
{
    // get file info
    struct stat statBuf;
    if ( state.config.syscall.fstat(fd, &statBuf) != 0 ) {
        int statErr = errno;
        if ( (statErr == EPERM) && state.config.syscall.sandboxBlockedStat(path) )
            diag.error("file system sandbox blocked stat(\"%s\")", path);
        else if ( statErr == ENOENT )
            diag.error("no such file");
        else
            diag.error("stat(\"%s\") failed with errno=%d", path, statErr);
        return -1;
    }

#if !__LP64__
    statBuf.st_ino = (statBuf.st_ino & 0xFFFFFFFF);
#endif

    // if inode/mtime was recorded, check that
    if ( fileValidation.checkInodeMtime ) {
        if ( statBuf.st_ino != fileValidation.inode ) {
            diag.error("file inode changed from 0x%llX to 0x%llX since PrebuiltLoader was built for '%s'", fileValidation.inode, statBuf.st_ino, path);
            return -1;
        }
        if ( (uint64_t)statBuf.st_mtime != fileValidation.mtime ) {
            diag.error("file mtime changed from 0x%llX to 0x%lX since PrebuiltLoader was built for '%s'", fileValidation.mtime, statBuf.st_mtime, path);
            return -1;
        }
        // sanity check slice offset
        if ( (uint64_t)statBuf.st_size < fileValidation.sliceOffset ) {
            diag.error("file too small for slice offset '%s'", path);
            return -1;
        }
        return fileValidation.sliceOffset;
    }
    else if ( codeSignature.size != 0 ) {
#if !TARGET_OS_SIMULATOR // some hashing functions not available in .a files
        // otherwise compare cdHash
        void* mappedFile = state.config.syscall.mmap(nullptr, (size_t)statBuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if ( mappedFile == MAP_FAILED ) {
            diag.error("could not mmap() '%s'", path);
            return -1;
        }
        uint64_t sliceOffset = -1;
        bool     isOSBinary  = false; // FIXME
        if ( const MachOFile* mf = MachOFile::compatibleSlice(diag, mappedFile, (size_t)statBuf.st_size, path, state.config.process.platform, isOSBinary, *state.config.process.archs) ) {
            const MachOLoaded* ml            = (MachOLoaded*)mf;
            __block bool       cdHashMatches = false;
            // Note, file is not mapped with zero fill so cannot use forEachCdHash()
            // need to use lower level forEachCDHashOfCodeSignature() which takes pointer to code blob
            ml->forEachCDHashOfCodeSignature((uint8_t*)mf + codeSignature.fileOffset, codeSignature.size, ^(const uint8_t cdHash[20]) {
                if ( ::memcmp((void*)cdHash, (void*)fileValidation.cdHash, 20) == 0 )
                    cdHashMatches = true;
            });
            if ( cdHashMatches )
                sliceOffset = (uint8_t*)mf - (uint8_t*)mappedFile;
            else
                diag.error("file cdHash not as expected '%s'", path);
        }
        state.config.syscall.munmap(mappedFile, (size_t)fileValidation.sliceOffset);
        return sliceOffset;
#endif
    }
    return -1;
}

const MachOAnalyzer* Loader::mapSegments(Diagnostics& diag, RuntimeState& state, const char* path, uint64_t vmSpace,
                                         const CodeSignatureInFile& codeSignature, bool hasCodeSignature,
                                         const Array<Region>& regions, bool neverUnloads, bool prebuilt, const FileValidationInfo& fileValidation)
{
#if BUILDING_DYLD
    dyld3::ScopedTimer timer(DBG_DYLD_TIMING_MAP_IMAGE, path, 0, 0);
#endif

    // open file
    int fd = state.config.syscall.open(path, O_RDONLY, 0);
    if ( fd == -1 ) {
        int openErr = errno;
        if ( (openErr == EPERM) && state.config.syscall.sandboxBlockedOpen(path) )
            diag.error("file system sandbox blocked open(\"%s\", O_RDONLY)", path);
        else if ( openErr == ENOENT )
            diag.error("no such file");
        else
            diag.error("open(\"%s\", O_RDONLY) failed with errno=%d", path, openErr);
        return nullptr;
    }

    // validated this file has not changed (since PrebuiltLoader was made)
    uint64_t sliceOffset = fileValidation.sliceOffset;
    if ( prebuilt ) {
        sliceOffset = validateFile(diag, state, fd, path, codeSignature, fileValidation);
        if ( diag.hasError() ) {
            state.config.syscall.close(fd);
            return nullptr;
        }
    }

#if BUILDING_DYLD
    // register code signature
    uint64_t coveredCodeLength = UINT64_MAX;
    if ( hasCodeSignature && codeSignature.size != 0 ) {
        dyld3::ScopedTimer(DBG_DYLD_TIMING_ATTACH_CODESIGNATURE, 0, 0, 0);
        fsignatures_t siginfo;
        siginfo.fs_file_start = sliceOffset;                             // start of mach-o slice in fat file
        siginfo.fs_blob_start = (void*)(long)(codeSignature.fileOffset); // start of CD in mach-o file
        siginfo.fs_blob_size  = codeSignature.size;                      // size of CD
        int result            = state.config.syscall.fcntl(fd, F_ADDFILESIGS_RETURN, &siginfo);
        if ( result == -1 ) {
            int errnoCopy = errno;
            if ( (errnoCopy == EPERM) || (errnoCopy == EBADEXEC) ) {
                diag.error("code signature invalid (errno=%d) sliceOffset=0x%08llX, codeBlobOffset=0x%08X, codeBlobSize=0x%08X for '%s'",
                           errnoCopy, sliceOffset, codeSignature.fileOffset, codeSignature.size, path);
            }
            else {
                diag.error("fcntl(fd, F_ADDFILESIGS_RETURN) failed with errno=%d, sliceOffset=0x%08llX, codeBlobOffset=0x%08X, codeBlobSize=0x%08X for '%s'",
                           errnoCopy, sliceOffset, codeSignature.fileOffset, codeSignature.size, path);
            }
            state.config.syscall.close(fd);
            return nullptr;
        }
        coveredCodeLength = siginfo.fs_file_start;
        if ( coveredCodeLength < codeSignature.fileOffset ) {
            diag.error("code signature does not cover entire file up to signature");
            state.config.syscall.close(fd);
            return nullptr;
        }
    }

    // <rdar://problem/41015217> dyld should use F_CHECK_LV even on unsigned binaries
    {
        // <rdar://problem/32684903> always call F_CHECK_LV to preflight
        fchecklv checkInfo;
        char     messageBuffer[512];
        messageBuffer[0]                = '\0';
        checkInfo.lv_file_start         = sliceOffset;
        checkInfo.lv_error_message_size = sizeof(messageBuffer);
        checkInfo.lv_error_message      = messageBuffer;
        int res                         = state.config.syscall.fcntl(fd, F_CHECK_LV, &checkInfo);
        if ( res == -1 ) {
            // rdar://79796526 (include uuid of mis-signed binary to help debug)
            char uuidStr[64];
            strcpy(uuidStr, "no uuid");
            mach_header mh;
            if ( state.config.syscall.pread(fd, &mh, sizeof(mh), (size_t)sliceOffset) == sizeof(mh) ) {
                if ( ((MachOFile*)&mh)->hasMachOMagic() ) {
                    size_t headerAndLoadCommandsSize = mh.sizeofcmds+sizeof(mach_header_64);
                    uint8_t buffer[headerAndLoadCommandsSize];
                    if ( state.config.syscall.pread(fd, buffer, sizeof(buffer), (size_t)sliceOffset) == headerAndLoadCommandsSize ) {
                        uuid_t uuid;
                        if ( ((MachOFile*)buffer)->getUuid(uuid) ) {
                            Loader::uuidToStr(uuid, uuidStr);
                        }
                    }
                }
            }
            diag.error("code signature in <%s> '%s' not valid for use in process: %s", uuidStr, path, messageBuffer);
            state.config.syscall.close(fd);
            return nullptr;
        }
    }
#endif

#if BUILDING_DYLD && SUPPORT_ROSETTA
    // if translated, need to add in translated code segment
    char     aotPath[PATH_MAX];
    uint64_t extraAllocSize = 0;
    if ( state.config.process.isTranslated ) {
        int ret = aot_get_extra_mapping_info(fd, path, extraAllocSize, aotPath, sizeof(aotPath));
        if ( ret == 0 ) {
            vmSpace += extraAllocSize;
        }
        else {
            extraAllocSize = 0;
            aotPath[0]     = '\0';
        }
    }
#endif

    // reserve address range
    vm_address_t  loadAddress = 0;
    kern_return_t r           = ::vm_allocate(mach_task_self(), &loadAddress, (vm_size_t)vmSpace, VM_FLAGS_ANYWHERE);
    if ( r != KERN_SUCCESS ) {
        diag.error("vm_allocate(size=0x%0llX) failed with result=%d", vmSpace, r);
        state.config.syscall.close(fd);
        return nullptr;
    }

#if BUILDING_DYLD
    if ( state.config.log.segments ) {
        if ( sliceOffset != 0 )
            state.log("Mapping %s (slice offset=0x%llX)\n", path, sliceOffset);
        else
            state.log("Mapping %s\n", path);
    }
#endif

    // map each segment
    bool             mmapFailure               = false;
    const uint8_t*   codeSignatureStartAddress = nullptr;
    const uint8_t*   linkeditEndAddress        = nullptr;
    __block uint32_t segIndex                  = 0;
    for ( const Region& region : regions ) {
        // <rdar://problem/32363581> Mapping zero filled regions fails with mmap of size 0
        if ( region.isZeroFill || (region.fileSize == 0) )
            continue;
        if ( (region.vmOffset == 0) && (segIndex > 0) )
            continue;
        int perms = VM_PROT_READ;
#if BUILDING_DYLD
        perms = region.perms;
#endif
        void* segAddress = state.config.syscall.mmap((void*)(loadAddress + region.vmOffset), (size_t)region.fileSize, perms,
                                                     MAP_FIXED | MAP_PRIVATE, fd, (size_t)(sliceOffset + region.fileOffset));
        int   mmapErr    = errno;
        if ( segAddress == MAP_FAILED ) {
            if ( mmapErr == EPERM ) {
                if ( state.config.syscall.sandboxBlockedMmap(path) )
                    diag.error("file system sandbox blocked mmap() of '%s'", path);
                else
                    diag.error("code signing blocked mmap() of '%s'", path);
            }
            else {
                diag.error("mmap(addr=0x%0llX, size=0x%08X) failed with errno=%d for %s", loadAddress + region.vmOffset,
                           region.fileSize, mmapErr, path);
            }
            mmapFailure = true;
            break;
        }
        else if ( codeSignature.fileOffset > region.fileOffset ) {
            codeSignatureStartAddress = (uint8_t*)segAddress + (codeSignature.fileOffset - region.fileOffset);
            linkeditEndAddress        = (uint8_t*)segAddress + region.fileSize;
        }
        // sanity check first segment is mach-o header
        if ( !mmapFailure && (segIndex == 0) ) {
            const MachOAnalyzer* ma = (MachOAnalyzer*)segAddress;
            if ( !ma->isMachO(diag, region.fileSize) ) {
                mmapFailure = true;
                break;
            }
        }
        if ( !mmapFailure ) {
#if BUILDING_DYLD
            uintptr_t mappedSize  = round_page((uintptr_t)region.fileSize);
            uintptr_t mappedStart = (uintptr_t)segAddress;
            uintptr_t mappedEnd   = mappedStart + mappedSize;
            if ( state.config.log.segments ) {
                const MachOLoaded* lmo = (MachOLoaded*)loadAddress;
                state.log("%14s (%c%c%c) 0x%012lX->0x%012lX\n", lmo->segmentName(segIndex),
                          (region.perms & PROT_READ) ? 'r' : '.', (region.perms & PROT_WRITE) ? 'w' : '.', (region.perms & PROT_EXEC) ? 'x' : '.',
                          mappedStart, mappedEnd);
            }
#endif
        }
        ++segIndex;
    }

#if BUILDING_DYLD && !TARGET_OS_SIMULATOR && (__arm64__ || __arm__)
    if ( !mmapFailure ) {
        // tell kernel about fairplay encrypted regions
        uint32_t fpTextOffset;
        uint32_t fpSize;
        const MachOAnalyzer* ma = (const MachOAnalyzer*)loadAddress;
        // FIXME: record if FP info in PrebuiltLoader
        if ( ma->isFairPlayEncrypted(fpTextOffset, fpSize) ) {
            int result = state.config.syscall.mremap_encrypted((void*)(loadAddress + fpTextOffset), fpSize, 1, ma->cputype, ma->cpusubtype);
            if ( result != 0 ) {
                diag.error("could not register fairplay decryption, mremap_encrypted() => %d", result);
                mmapFailure = true;
            }
        }
    }
#endif

    if ( mmapFailure ) {
        ::vm_deallocate(mach_task_self(), loadAddress, (vm_size_t)vmSpace);
        state.config.syscall.close(fd);
        return nullptr;
    }

#if BUILDING_DYLD && SUPPORT_ROSETTA
    if ( state.config.process.isTranslated && (extraAllocSize != 0) ) {
        // map in translated code at end of mapped segments
        dyld_aot_image_info aotInfo;
        uint64_t            extraSpaceAddr = (long)loadAddress + vmSpace - extraAllocSize;
        int                 ret            = aot_map_extra(path, (mach_header*)loadAddress, (void*)extraSpaceAddr, aotInfo.aotLoadAddress, aotInfo.aotImageSize, aotInfo.aotImageKey);
        if ( ret == 0 ) {
            // fill in the load address, at this point the Rosetta trap has filled in the other fields
            aotInfo.x86LoadAddress = (mach_header*)loadAddress;
            addAotImagesToAllAotImages(state.longTermAllocator, 1, &aotInfo);
            if ( state.config.log.segments ) {
                state.log("%14s (r.x) 0x%012llX->0x%012llX\n", "ROSETTA", extraSpaceAddr, extraSpaceAddr + extraAllocSize);
            }
        }
    }
#endif
    // close file
    state.config.syscall.close(fd);
#if BUILDING_DYLD
    if ( state.config.log.libraries ) {
        Loader::logLoad(state, (MachOLoaded*)loadAddress, path);
    }
#endif
    return (MachOAnalyzer*)loadAddress;
}

void Loader::applyFixupsGeneric(Diagnostics& diag, RuntimeState& state, const Array<const void*>& bindTargets,
                                const Array<const void*>& overrideBindTargets, bool laziesMustBind,
                                const Array<MissingFlatLazySymbol>& missingFlatLazySymbols) const
{
    const MachOAnalyzer* ma    = (MachOAnalyzer*)this->loadAddress(state);
    const uintptr_t      slide = ma->getSlide();
    if ( ma->hasChainedFixups() ) {
        // walk all chains
        ma->withChainStarts(diag, ma->chainStartsOffset(), ^(const dyld_chained_starts_in_image* startsInfo) {
            ma->fixupAllChainedFixups(diag, startsInfo, slide, bindTargets, ^(void* loc, void* newValue) {
                if ( state.config.log.fixups )
                    state.log("fixup: *0x%012lX = 0x%012lX\n", (uintptr_t)loc, (uintptr_t)newValue);
                *((uintptr_t*)loc) = (uintptr_t)newValue;
            });
        });
    }
    else if ( ma->hasOpcodeFixups() ) {
        // process all rebase opcodes
        ma->forEachRebaseLocation_Opcodes(diag, ^(uint64_t runtimeOffset, bool& stop) {
            uintptr_t* loc      = (uintptr_t*)((uint8_t*)ma + runtimeOffset);
            uintptr_t  locValue = *loc;
            uintptr_t  newValue = locValue + slide;
            if ( state.config.log.fixups )
                state.log("fixup: *0x%012lX = 0x%012lX <rebase>\n", (uintptr_t)loc, (uintptr_t)newValue);
            *loc = newValue;
        });
        if ( diag.hasError() )
            return;

        // process all bind opcodes
        ma->forEachBindLocation_Opcodes(diag, ^(uint64_t runtimeOffset, unsigned targetIndex, bool& stop) {
            uintptr_t* loc      = (uintptr_t*)((uint8_t*)ma + runtimeOffset);
            uintptr_t  newValue = (uintptr_t)(bindTargets[targetIndex]);

            if ( state.config.log.fixups )
                state.log("fixup: *0x%012lX = 0x%012lX <%s/bind#%u>\n", (uintptr_t)loc, (uintptr_t)newValue, this->leafName(), targetIndex);
            *loc = newValue;

            // Record missing lazy symbols
            if ( newValue == (uintptr_t)state.libdyldMissingSymbol ) {
                for (const MissingFlatLazySymbol& missingSymbol : missingFlatLazySymbols) {
                    if ( missingSymbol.bindTargetIndex == targetIndex ) {
                        state.addMissingFlatLazySymbol(this, missingSymbol.symbolName, loc);
                        break;
                    }
                }
            }
        }, ^(uint64_t runtimeOffset, unsigned overrideBindTargetIndex, bool& stop) {
            uintptr_t* loc      = (uintptr_t*)((uint8_t*)ma + runtimeOffset);
            uintptr_t  newValue = (uintptr_t)(overrideBindTargets[overrideBindTargetIndex]);

            // Skip missing weak binds
            if ( newValue == UINTPTR_MAX ) {
                if ( state.config.log.fixups )
                    state.log("fixup: *0x%012lX (skipping missing weak bind) <%s/weak-bind#%u>\n", (uintptr_t)loc, this->leafName(), overrideBindTargetIndex);
                return;
            }

            if ( state.config.log.fixups )
                state.log("fixup: *0x%012lX = 0x%012lX <%s/weak-bind#%u>\n", (uintptr_t)loc, (uintptr_t)newValue, this->leafName(), overrideBindTargetIndex);
            *loc = newValue;
        });
    }
    else {
        // process internal relocations
        ma->forEachRebaseLocation_Relocations(diag, ^(uint64_t runtimeOffset, bool& stop) {
            uintptr_t* loc      = (uintptr_t*)((uint8_t*)ma + runtimeOffset);
            uintptr_t  locValue = *loc;
            uintptr_t  newValue = locValue + slide;
            if ( state.config.log.fixups )
                state.log("fixup: *0x%012lX = 0x%012lX <rebase>\n", (uintptr_t)loc, (uintptr_t)newValue);
            *loc = newValue;
        });
        if ( diag.hasError() )
            return;

        // process external relocations
        ma->forEachBindLocation_Relocations(diag, ^(uint64_t runtimeOffset, unsigned targetIndex, bool& stop) {
            uintptr_t* loc      = (uintptr_t*)((uint8_t*)ma + runtimeOffset);
            uintptr_t  newValue = (uintptr_t)(bindTargets[targetIndex]);
            if ( state.config.log.fixups )
                state.log("fixup: *0x%012lX = 0x%012lX <%s/bind#%u>\n", (uintptr_t)loc, (uintptr_t)newValue, this->leafName(), targetIndex);
            *loc = newValue;
        });
    }
}

void Loader::findAndRunAllInitializers(RuntimeState& state) const
{
    Diagnostics                           diag;
    const MachOAnalyzer*                  ma              = this->analyzer(state);
    dyld3::MachOAnalyzer::VMAddrConverter vmAddrConverter = ma->makeVMAddrConverter(true);
    ma->forEachInitializer(diag, vmAddrConverter, ^(uint32_t offset) {
        Initializer func = (Initializer)((uint8_t*)ma + offset);
        if ( state.config.log.initializers )
            state.log("running initializer %p in %s\n", func, this->path());
#if __has_feature(ptrauth_calls)
        func = __builtin_ptrauth_sign_unauthenticated(func, ptrauth_key_asia, 0);
#endif
        dyld3::ScopedTimer(DBG_DYLD_TIMING_STATIC_INITIALIZER, (uint64_t)ma, (uint64_t)func, 0);
        func(state.config.process.argc, state.config.process.argv, state.config.process.envp, state.config.process.apple, state.vars);
    });
}

void Loader::runInitializersBottomUp(RuntimeState& state, Array<const Loader*>& danglingUpwards) const
{
    // do nothing if already initializers already run
    if ( (const_cast<Loader*>(this))->beginInitializers(state) )
        return;

    //state.log("runInitializersBottomUp(%s)\n", this->path());

    // make sure everything below this image is initialized before running my initializers
    const uint32_t depCount = this->dependentCount();
    for ( uint32_t i = 0; i < depCount; ++i ) {
        DependentKind childKind;
        if ( Loader* child = this->dependent(state, i, &childKind) ) {
            if ( childKind == DependentKind::upward ) {
                // add upwards to list to process later
                if ( !danglingUpwards.contains(child) )
                    danglingUpwards.push_back(child);
            }
            else {
                child->runInitializersBottomUp(state, danglingUpwards);
            }
        }
    }

    // tell objc to run any +load methods in this image (done before C++ initializers)
    state.notifyObjCInit(this);

    // run initializers for this image
    this->runInitializers(state);
}

void Loader::runInitializersBottomUpPlusUpwardLinks(RuntimeState& state) const
{
    //state.log("runInitializersBottomUpPlusUpwardLinks() %s\n", this->path());
    state.incWritable();

    // recursively run all initializers
    STACK_ALLOC_ARRAY(const Loader*, danglingUpwards, state.loaded.size());
    this->runInitializersBottomUp(state, danglingUpwards);

    //state.log("runInitializersBottomUpPlusUpwardLinks(%s), found %d dangling upwards\n", this->path(), danglingUpwards.count());

    // go back over all images that were upward linked, and recheck they were initialized (might be danglers)
    STACK_ALLOC_ARRAY(const Loader*, extraDanglingUpwards, state.loaded.size());
    for ( const Loader* ldr : danglingUpwards ) {
        //state.log("running initializers for dangling upward link %s\n", ldr->path());
        ldr->runInitializersBottomUp(state, extraDanglingUpwards);
    }
    if ( !extraDanglingUpwards.empty() ) {
        // incase of double upward dangling images, check initializers again
        danglingUpwards.resize(0);
        for ( const Loader* ldr : extraDanglingUpwards ) {
            //state.log("running initializers for dangling upward link %s\n", ldr->path());
            ldr->runInitializersBottomUp(state, danglingUpwards);
        }
    }

    state.decWritable();
}

void Loader::makeSegmentsReadOnly(RuntimeState& state) const
{
    const MachOAnalyzer* ma    = this->analyzer(state);
    uintptr_t            slide = ma->getSlide();
    ma->forEachSegment(^(const MachOAnalyzer::SegmentInfo& segInfo, bool& stop) {
        if ( segInfo.readOnlyData ) {
            const uint8_t* start = (uint8_t*)(segInfo.vmAddr + slide);
            size_t         size  = (size_t)segInfo.vmSize;
            state.config.syscall.mprotect((void*)start, size, PROT_READ);
            if ( state.config.log.segments )
                state.log("mprotect 0x%012lX->0x%012lX to read-only\n", (long)start, (long)start + size);
        }
    });
}


void Loader::logSegmentsFromSharedCache(RuntimeState& state) const
{
    state.log("Using mapping in dyld cache for %s\n", this->path());
    uint64_t cacheSlide = state.config.dyldCache.slide;
    this->loadAddress(state)->forEachSegment(^(const MachOLoaded::SegmentInfo& info, bool& stop) {
        state.log("%14s (%c%c%c) 0x%012llX->0x%012llX \n", info.segName,
                  (info.readable() ? 'r' : '.'), (info.writable() ? 'w' : '.'), (info.executable() ? 'x' : '.'),
                  info.vmAddr + cacheSlide, info.vmAddr + cacheSlide + info.vmSize);
    });
}

// FIXME:  This only handles weak-defs and does not look for non-weaks that override weak-defs
void Loader::addWeakDefsToMap(RuntimeState& state, const dyld3::Array<const Loader*>& newLoaders)
{
    for (const Loader* ldr : newLoaders) {
        const MachOAnalyzer* ma = ldr->analyzer(state);
        if ( (ma->flags & MH_WEAK_DEFINES) == 0 )
            continue;
        if ( ldr->hiddenFromFlat() )
            continue;

        // NOTE: using the nlist is faster to scan for weak-def exports, than iterating the exports trie
        Diagnostics diag;
        uint64_t    baseAddress = ma->preferredLoadAddress();
        ma->forEachGlobalSymbol(diag, ^(const char* symbolName, uint64_t n_value, uint8_t n_type, uint8_t n_sect, uint16_t n_desc, bool& stop) {
            if ( (n_desc & N_WEAK_DEF) != 0 ) {
                // only add if not already in map
                const auto& pos = state.weakDefMap->find(symbolName);
                if ( pos == state.weakDefMap->end() ) {
                    WeakDefMapValue mapEntry;
                    mapEntry.targetLoader        = ldr;
                    mapEntry.targetRuntimeOffset = n_value - baseAddress;
                    mapEntry.isCode              = false;  // unused
                    mapEntry.isWeakDef           = true;
                    state.weakDefMap->operator[](symbolName) = mapEntry;
                }
            }
        });
    }
}

Loader::ResolvedSymbol Loader::resolveSymbol(Diagnostics& diag, RuntimeState& state, int libOrdinal, const char* symbolName,
                                             bool weakImport, bool lazyBind, CacheWeakDefOverride patcher, bool buildingCache) const
{
    __block ResolvedSymbol result = { nullptr, symbolName, 0, ResolvedSymbol::Kind::bindAbsolute, false, false };
    if ( (libOrdinal > 0) && ((unsigned)libOrdinal <= this->dependentCount()) ) {
        result.targetLoader = dependent(state, libOrdinal - 1);
    }
    else if ( libOrdinal == BIND_SPECIAL_DYLIB_SELF ) {
        result.targetLoader = this;
    }
    else if ( libOrdinal == BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE ) {
        result.targetLoader = state.mainExecutableLoader;
    }
    else if ( libOrdinal == BIND_SPECIAL_DYLIB_FLAT_LOOKUP ) {
        __block bool found = false;
        state.withLoadersReadLock(^{
            for ( const Loader* ldr : state.loaded ) {
                // flat lookup can look in self, even if hidden
                if ( ldr->hiddenFromFlat() && (ldr != this) )
                    continue;
                if ( ldr->hasExportedSymbol(diag, state, symbolName, Loader::shallow, &result) ) {
                    found = true;
                    return;
                }
            }
        });
        if ( found ) {
            // record the dynamic dependency so the symbol we found does not get unloaded from under us
            if ( result.targetLoader != this )
                state.addDynamicReference(this, result.targetLoader);
        }
        else {
            if ( weakImport ) {
                // ok to be missing, bind to NULL
                result.kind                = ResolvedSymbol::Kind::bindAbsolute;
                result.targetRuntimeOffset = 0;
            }
            else if ( lazyBind && (state.libdyldMissingSymbol != nullptr) ) {
                // lazy bound symbols can be bound to __dyld_missing_symbol_abort
                result.targetLoader        = state.libdyldLoader;
                result.targetSymbolName    = symbolName;
                result.targetRuntimeOffset = (uintptr_t)state.libdyldMissingSymbol - (uintptr_t)state.libdyldLoader->loadAddress(state);
                result.kind                = ResolvedSymbol::Kind::bindToImage;
                result.isCode              = false; // only used for arm64e which uses trie not nlist
                result.isWeakDef           = false;
            }
            else {
                // missing symbol, but not weak-import or lazy-bound, so error
                diag.error("symbol not found in flat namespace '%s'", symbolName);
            }
        }
        return result;
    }
    else if ( libOrdinal == BIND_SPECIAL_DYLIB_WEAK_LOOKUP ) {
        const bool             verboseWeak        = false;
        const DyldSharedCache* dyldCache          = state.config.dyldCache.addr;
        __block bool           foundFirst         = false;
        __block bool           foundFirstInCache  = false;
#if BUILDING_CACHE_BUILDER
        if ( buildingCache ) {
            // when dylibs in cache are build, we don't have real load order, so do weak binding differently
            if ( verboseWeak )
                state.log("looking for weak-def symbol %s\n", symbolName);

            // look first in /usr/lib/libc++, most will be here
            for ( const Loader* ldr : state.loaded ) {
                ResolvedSymbol libcppResult;
                if ( ldr->loadAddress(state)->hasWeakDefs() && (strncmp(ldr->path(), "/usr/lib/libc++.", 16) == 0) ) {
                    if ( ldr->hasExportedSymbol(diag, state, symbolName, Loader::shallow, &libcppResult) ) {
                        if ( verboseWeak )
                            state.log("  using %s from libc++.dylib\n", symbolName);
                        return libcppResult;
                    }
                }
            }

            // if not found, try looking in the images itself, most custom weak-def symbols have a copy in the image itself
            ResolvedSymbol selfResult;
            if ( this->hasExportedSymbol(diag, state, symbolName, Loader::shallow, &selfResult) ) {
                if ( verboseWeak )
                    state.log("  using %s from self %s\n", symbolName, this->path());
                return selfResult;
            }

            // if this image directly links with something that also defines this weak-def, use that because we know it will be loaded
            const uint32_t depCount = this->dependentCount();
            for ( uint32_t i = 0; i < depCount; ++i ) {
                Loader::DependentKind depKind;
                if ( Loader* depLoader = this->dependent(state, i, &depKind) ) {
                    if ( depKind != Loader::DependentKind::upward ) {
                        ResolvedSymbol depResult;
                        if ( depLoader->hasExportedSymbol(diag, state, symbolName, Loader::staticLink, &depResult) ) {
                            if ( verboseWeak )
                                state.log("  using %s from dependent %s\n", symbolName, depLoader->path());
                            return depResult;
                        }
                    }
                }
            }

            // no impl??
            diag.error("weak-def symbol (%s) not found in dyld cache", symbolName);
            return result;
        }
        else // fall into app launch case
#endif
        state.withLoadersReadLock(^{
            if ( verboseWeak )
                state.log("looking for weak-def symbol %s\n", symbolName);
            state.weakDefResolveSymbolCount++;
            // 5000 is a guess that "this is a large C++ app" and could use a map to speed up coalescing
            if ( (state.weakDefResolveSymbolCount > 5000) && (state.weakDefMap == nullptr) ) {
                state.weakDefMap = new (state.longTermAllocator.malloc(sizeof(WeakDefMap))) WeakDefMap();
            }
            if ( state.weakDefMap != nullptr ) {
                const auto& pos = state.weakDefMap->find(symbolName);
                if ( (pos != state.weakDefMap->end()) && (pos->second.targetLoader != nullptr) ) {
                    //state.log("resolveSymbol(%s) found in map\n", symbolName);
                    result.targetLoader        = pos->second.targetLoader;
                    result.targetSymbolName    = symbolName;
                    result.targetRuntimeOffset = pos->second.targetRuntimeOffset;
                    result.kind                = ResolvedSymbol::Kind::bindToImage;
                    result.isCode              = pos->second.isCode;
                    result.isWeakDef           = pos->second.isWeakDef;
                    if ( verboseWeak )
                        state.log("  found %s in map, using impl from %s\n", symbolName, result.targetLoader->path());
                    foundFirst = true;
                    return;
                }
            }

            // Keep track of results from the cache to be processed at the end, once
            // we've chosen a canonical definition
            struct CacheLookupResult {
                const Loader*   targetLoader        = nullptr;
                uint64_t        targetRuntimeOffset = 0;
            };
            STACK_ALLOC_ARRAY(CacheLookupResult, cacheResults, state.loaded.size());

            bool weakBindOpcodeClient = !this->dylibInDyldCache && this->analyzer(state)->hasOpcodeFixups();
            for ( const Loader* ldr : state.loaded ) {
                if ( ldr->loadAddress(state)->flags & MH_WEAK_DEFINES ) {
                    ResolvedSymbol thisResult;
                    // weak coalescing ignores hidden images
                    if ( ldr->hiddenFromFlat() )
                        continue;
                    if ( ldr->hasExportedSymbol(diag, state, symbolName, Loader::shallow, &thisResult) ) {
                        if ( weakBindOpcodeClient && !thisResult.isWeakDef && ldr->dylibInDyldCache ) {
                            // rdar://75956202 ignore non-weak symbols in shared cache when opcode based binary is looking for symbols to coalesce
                            continue;
                        }
                        if ( thisResult.targetLoader->dylibInDyldCache && !ldr->hasBeenFixedUp(state) )
                            cacheResults.push_back({ thisResult.targetLoader, thisResult.targetRuntimeOffset });

                        // record first implementation found, but keep searching
                        if ( !foundFirst ) {
                            foundFirst        = true;
                            result            = thisResult;
                            foundFirstInCache = thisResult.targetLoader->dylibInDyldCache;
                            if ( verboseWeak )
                                state.log("  using %s in %s\n", symbolName, thisResult.targetLoader->path());
                        }
                        if ( !thisResult.isWeakDef && result.isWeakDef ) {
                            // non-weak wins over previous weak-def
                            // we don't stop search because we need to see if this overrides anything in the dyld cache
                            result = thisResult;
                            if ( verboseWeak )
                                state.log("  using non-weak %s in %s\n", symbolName, thisResult.targetLoader->path());
                        }
                    }
                }
            }
            // if not found anywhere else and this image is hidden, try looking in itself
            if ( !foundFirst && this->hiddenFromFlat() ) {
                if ( verboseWeak )
                        state.log("  did not find unhidden %s, trying self (%s)\n", symbolName, this->leafName());
                ResolvedSymbol thisResult;
                if ( this->hasExportedSymbol(diag, state, symbolName, Loader::shallow, &thisResult) ) {
                    foundFirst = true;
                    result     = thisResult;
                }
            }

            // Patch the cache if we chose a definition which overrides it
            if ( foundFirst && !cacheResults.empty() && !result.targetLoader->dylibInDyldCache && (patcher != nullptr) ) {
                uint64_t patchedCacheOffset = 0;
                for ( const CacheLookupResult& cacheResult : cacheResults ) {
                    // We have already found the impl which we want all clients to use.
                    // But, later in load order we see something in the dyld cache that also implements
                    // this symbol, so we need to change all caches uses of that to use the found one instead.
                    const MachOLoaded* cacheML = cacheResult.targetLoader->loadAddress(state);
                    uint32_t           cachedOverriddenDylibIndex;
                    if ( dyldCache->findMachHeaderImageIndex(cacheML, cachedOverriddenDylibIndex) ) {
                        uint64_t cacheOverriddenExportOffset = ((uint64_t)cacheML + cacheResult.targetRuntimeOffset - (uint64_t)dyldCache);
                        if ( cacheOverriddenExportOffset != patchedCacheOffset ) {
                            // because of re-exports, same cacheOffset shows up in multiple dylibs.  Only call patcher once per
                            if ( verboseWeak )
                                state.log("  found use of %s in cache, need to override: %s\n", symbolName, cacheResult.targetLoader->path());
                            patcher(cachedOverriddenDylibIndex, (uint32_t)cacheResult.targetRuntimeOffset, result);
                            patchedCacheOffset = cacheOverriddenExportOffset;
                        }
                    }
                }
            }
        });
        if ( foundFirst ) {
            // if a c++ dylib weak-def binds to another dylibs, record the dynamic dependency
            if ( result.targetLoader != this )
                state.addDynamicReference(this, result.targetLoader);
            // if we are using a map to cache weak-def resolution, add to map
            if ( (state.weakDefMap != nullptr) && !result.targetLoader->hiddenFromFlat() ) {
                WeakDefMapValue mapEntry;
                mapEntry.targetLoader        = result.targetLoader;
                mapEntry.targetRuntimeOffset = result.targetRuntimeOffset;
                mapEntry.isCode              = result.isCode;
                mapEntry.isWeakDef           = result.isWeakDef;
                state.weakDefMap->operator[](symbolName) = mapEntry;
            }
        }
        else {
            if ( weakImport ) {
                // ok to be missing, bind to NULL
                result.kind                = ResolvedSymbol::Kind::bindAbsolute;
                result.targetRuntimeOffset = 0;
            }
            else {
                diag.error("weak-def symbol not found '%s'", symbolName);
            }
        }
        return result;
    }
    else {
        diag.error("unknown library ordinal %d in %s when binding '%s'", libOrdinal, path(), symbolName);
        return result;
    }
    if ( result.targetLoader != nullptr ) {
        STACK_ALLOC_ARRAY(const Loader*, alreadySearched, state.loaded.size());
        if ( result.targetLoader->hasExportedSymbol(diag, state, symbolName, Loader::staticLink, &result, &alreadySearched) ) {
            return result;
        }
    }
    if ( weakImport ) {
        // ok to be missing, bind to NULL
        result.kind                = ResolvedSymbol::Kind::bindAbsolute;
        result.targetRuntimeOffset = 0;
    }
    else if ( lazyBind && (state.libdyldMissingSymbol != nullptr) ) {
        // missing lazy binds are bound to abort
        result.targetLoader        = state.libdyldLoader;
        result.targetSymbolName    = symbolName;
        result.targetRuntimeOffset = (uintptr_t)state.libdyldMissingSymbol - (uintptr_t)state.libdyldLoader->loadAddress(state);
        result.kind                = ResolvedSymbol::Kind::bindToImage;
        result.isCode              = false; // only used for arm64e which uses trie not nlist
        result.isWeakDef           = false;
    }
    else {
        // if libSystem.dylib has not been initialized yet, then the missing symbol is during launch and need to save that info
        const char* expectedInDylib = "unknown";
        if ( result.targetLoader != nullptr )
            expectedInDylib = result.targetLoader->path();
#if BUILDING_DYLD
        if ( !gProcessInfo->libSystemInitialized ) {
            state.setLaunchMissingSymbol(symbolName, expectedInDylib, this->path());
        }
#endif
        // FIXME: check for too-new binary

        diag.error("Symbol not found: %s\n  Referenced from: %s\n  Expected in: %s", symbolName, this->path(), expectedInDylib);
    }
    return result;
}

bool Loader::hasExportedSymbol(Diagnostics& diag, RuntimeState& state, const char* symbolName, ExportedSymbolMode mode, ResolvedSymbol* result, dyld3::Array<const Loader*>* alreadySearched) const
{
    // don't search twice
    if ( alreadySearched != nullptr ) {
        for ( const Loader* im : *alreadySearched ) {
            if ( im == this )
                return false;
        }
        alreadySearched->push_back(this);
    }
    bool               canSearchDependents;
    bool               searchNonReExports;
    bool               searchSelf;
    ExportedSymbolMode depsMode;
    switch ( mode ) {
        case staticLink:
            canSearchDependents = true;
            searchNonReExports  = false;
            searchSelf          = true;
            depsMode            = staticLink;
            break;
        case shallow:
            canSearchDependents = false;
            searchNonReExports  = false;
            searchSelf          = true;
            depsMode            = shallow;
            break;
        case dlsymNext:
            canSearchDependents = true;
            searchNonReExports  = true;
            searchSelf          = false;
            depsMode            = dlsymSelf;
            break;
        case dlsymSelf:
            canSearchDependents = true;
            searchNonReExports  = true;
            searchSelf          = true;
            depsMode            = dlsymSelf;
            break;
    }
    const MachOLoaded* ml = this->loadAddress(state);
    //state.log("Loader::hasExportedSymbol(%s) this=%s\n", symbolName, this->path());
    uint64_t trieRuntimeOffset;
    uint32_t trieSize;
    if ( this->getExportsTrie(trieRuntimeOffset, trieSize) ) {
        const uint8_t* trieStart = (uint8_t*)ml + trieRuntimeOffset;
        const uint8_t* trieEnd   = trieStart + trieSize;
        const uint8_t* node      = MachOLoaded::trieWalk(diag, trieStart, trieEnd, symbolName);
        //state.log("    trieStart=%p, trieSize=0x%08X, node=%p, error=%s\n", trieStart, trieSize, node, diag.errorMessage());
        if ( (node != nullptr) && searchSelf ) {
            const uint8_t* p     = node;
            const uint64_t flags = MachOLoaded::read_uleb128(diag, p, trieEnd);
            if ( flags & EXPORT_SYMBOL_FLAGS_REEXPORT ) {
                // re-export from another dylib, lookup there
                const uint64_t ordinal      = MachOLoaded::read_uleb128(diag, p, trieEnd);
                const char*    importedName = (char*)p;
                bool nameChanged = false;
                if ( importedName[0] == '\0' ) {
                    importedName = symbolName;
                } else {
                    nameChanged = true;
                }
                if ( (ordinal == 0) || (ordinal > this->dependentCount()) ) {
                    diag.error("re-export ordinal %lld in %s out of range for %s", ordinal, this->path(), symbolName);
                    return false;
                }
                uint32_t      depIndex = (uint32_t)(ordinal - 1);
                DependentKind depKind;
                if ( Loader* depLoader = this->dependent(state, depIndex, &depKind) ) {
                    if ( nameChanged && alreadySearched ) {
                        // As we are changing the symbol name we are looking for, use a new alreadySearched.  The existnig
                        // alreadySearched may include loaders we have searched before for the old name, but not the new one,
                        // and we want to check them again
                        STACK_ALLOC_ARRAY(const Loader*, nameChangedAlreadySearched, state.loaded.size());
                        return depLoader->hasExportedSymbol(diag, state, importedName, mode, result, &nameChangedAlreadySearched);
                    }
                    return depLoader->hasExportedSymbol(diag, state, importedName, mode, result, alreadySearched);
                }
                return false; // re-exported symbol from weak-linked dependent which is missing
            }
            else {
                if ( diag.hasError() )
                    return false;
                bool isAbsoluteSymbol       = ((flags & EXPORT_SYMBOL_FLAGS_KIND_MASK) == EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE);
                result->targetLoader        = this;
                result->targetSymbolName    = symbolName;
                result->targetRuntimeOffset = (uintptr_t)MachOLoaded::read_uleb128(diag, p, trieEnd);
                result->kind                = isAbsoluteSymbol ? ResolvedSymbol::Kind::bindAbsolute : ResolvedSymbol::Kind::bindToImage;
                result->isCode              = this->analyzer(state)->inCodeSection((uint32_t)(result->targetRuntimeOffset));
                result->isWeakDef           = (flags & EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION);
                return true;
            }
        }
    }
    else {
        // try old slow way
        const MachOAnalyzer* ma    = (MachOAnalyzer*)ml;
        __block bool         found = false;
        ma->forEachGlobalSymbol(diag, ^(const char* n_name, uint64_t n_value, uint8_t n_type, uint8_t n_sect, uint16_t n_desc, bool& stop) {
            if ( ((n_type & N_TYPE) == N_SECT) && ((n_type & N_EXT) != 0) ) {
                if ( strcmp(n_name, symbolName) == 0 ) {
                    result->targetLoader        = this;
                    result->targetSymbolName    = symbolName;
                    result->targetRuntimeOffset = (uintptr_t)(n_value - ma->preferredLoadAddress());
                    result->kind                = ResolvedSymbol::Kind::bindToImage;
                    result->isCode              = false; // only used for arm64e which uses trie not nlist
                    result->isWeakDef           = (n_desc & N_WEAK_DEF);
                    stop                        = true;
                    found                       = true;
                }
            }
        });
        if ( found )
            return true;
    }
    if ( canSearchDependents ) {
        // Search re-exported dylibs
        const uint32_t depCount = this->dependentCount();
        for ( uint32_t i = 0; i < depCount; ++i ) {
            Loader::DependentKind depKind;
            if ( Loader* depLoader = this->dependent(state, i, &depKind) ) {
                //state.log("dep #%d of %p is %d %p (%s %s)\n", i, this, (int)depKind, depLoader, this->path(), depLoader->path());
                if ( (depKind == Loader::DependentKind::reexport) || (searchNonReExports && (depKind != Loader::DependentKind::upward)) ) {
                    if ( depLoader->hasExportedSymbol(diag, state, symbolName, depsMode, result, alreadySearched) )
                        return true;
                }
            }
        }
    }
    return false;
}

uintptr_t Loader::resolvedAddress(RuntimeState& state, const ResolvedSymbol& symbol)
{
    switch ( symbol.kind ) {
        case ResolvedSymbol::Kind::rebase:
        case ResolvedSymbol::Kind::bindToImage:
            return (uintptr_t)symbol.targetLoader->loadAddress(state) + (uintptr_t)symbol.targetRuntimeOffset;
        case ResolvedSymbol::Kind::bindAbsolute:
            return (uintptr_t)symbol.targetRuntimeOffset;
    }
}



uintptr_t Loader::interpose(RuntimeState& state, uintptr_t value, const Loader* forLoader)
{
    // AMFI can ban interposing
    // Note we check this here just in case someone tried to substitute a fake interposing tuples array in the state
    if ( !state.config.security.allowInterposing )
        return value;

    // <rdar://problem/25686570> ignore interposing on a weak function that does not exist
    if ( value == 0 )
        return 0;

    // look for image specific interposing (needed for multiple interpositions on the same function)
    for ( const InterposeTupleSpecific& tuple : state.interposingTuplesSpecific ) {
        if ( (tuple.replacee == value) && (tuple.onlyImage == forLoader) ) {
            if ( state.config.log.interposing )
                state.log("  interpose replaced 0x%08lX with 0x%08lX in %s\n", value, tuple.replacement, forLoader->path());
            return tuple.replacement;
        }
    }

    // no image specific interpose, so look for generic interpose
    for ( const InterposeTupleAll& tuple : state.interposingTuplesAll ) {
        if ( tuple.replacee == value ) {
            if ( state.config.log.interposing )
                state.log("  interpose replaced 0x%08lX with 0x%08lX in %s\n", value, tuple.replacement, forLoader ? forLoader->path() : "dlsym");
            return tuple.replacement;
        }
    }
    return value;
}

#if BUILDING_DYLD
void Loader::applyInterposingToDyldCache(RuntimeState& state)
{
    const DyldSharedCache* dyldCache = state.config.dyldCache.addr;
    if ( dyldCache == nullptr )
        return; // no dyld cache to interpose
    if ( state.interposingTuplesAll.empty() )
        return; // no interposing tuples

    // make the cache writable for this block
    DyldCacheDataConstScopedWriter patcher(state);

    state.setVMAccountingSuspending(true);
    for ( const InterposeTupleAll& tuple : state.interposingTuplesAll ) {
        uint32_t imageIndex;
        uintptr_t cacheOffsetOfReplacee = tuple.replacee - (uintptr_t)dyldCache;
        if ( !dyldCache->addressInText(cacheOffsetOfReplacee, &imageIndex) )
            continue;

        // Convert from a cache offset to an image offset
        uint64_t mTime;
        uint64_t inode;
        const dyld3::MachOAnalyzer* imageMA = (dyld3::MachOAnalyzer*)(dyldCache->getIndexedImageEntry(imageIndex, mTime, inode));
        if ( imageMA == nullptr )
            continue;

        uint32_t dylibOffsetOfReplacee = (uint32_t)((dyldCache->unslidLoadAddress() + cacheOffsetOfReplacee) - imageMA->preferredLoadAddress());

        dyldCache->forEachPatchableExport(imageIndex, ^(uint32_t dylibVMOffsetOfImpl, const char* exportName) {
            // Skip patching anything other than this symbol
            if ( dylibVMOffsetOfImpl != dylibOffsetOfReplacee )
                return;
            uintptr_t newLoc = tuple.replacement;
            dyldCache->forEachPatchableUseOfExport(imageIndex, dylibVMOffsetOfImpl,
                                                   ^(uint64_t cacheVMOffset, MachOLoaded::PointerMetaData pmd, uint64_t addend) {
                uintptr_t* loc      = (uintptr_t*)((uintptr_t)dyldCache + cacheVMOffset);
                uintptr_t  newValue = newLoc + (uintptr_t)addend;
    #if __has_feature(ptrauth_calls)
                if ( pmd.authenticated ) {
                    newValue = dyld3::MachOLoaded::ChainedFixupPointerOnDisk::Arm64e::signPointer(newValue, loc, pmd.usesAddrDiversity, pmd.diversity, pmd.key);
                    *loc     = newValue;
                    if ( state.config.log.interposing )
                        state.log("interpose: *%p = %p (JOP: diversity 0x%04X, addr-div=%d, key=%s)\n",
                                  loc, (void*)newValue, pmd.diversity, pmd.usesAddrDiversity, MachOLoaded::ChainedFixupPointerOnDisk::Arm64e::keyName(pmd.key));
                    return;
                }
    #endif
                if ( state.config.log.interposing )
                    state.log("interpose: *%p = 0x%0llX (dyld cache patch) to %s\n", loc, newLoc + addend, exportName);
                *loc = newValue;
            });
        });
    }
    state.setVMAccountingSuspending(false);
}


void Loader::applyCachePatchesToOverride(RuntimeState& state, const Loader* dylibToPatch,
                                         uint16_t overriddenDylibIndex, const DylibPatch* patches,
                                         DyldCacheDataConstLazyScopedWriter& cacheDataConst) const
{
    const DyldSharedCache*  dyldCache           = state.config.dyldCache.addr;
    const MachOAnalyzer*    dylibToPatchMA      = dylibToPatch->analyzer(state);
    uint32_t                dylibToPatchIndex   = dylibToPatch->ref.index;

    // Early return if we have no exports used in the client dylib.  Then we don't need to walk every export
    if ( !dyldCache->shouldPatchClientOfImage(overriddenDylibIndex, dylibToPatchIndex) )
        return;

    assert(dyldCache->patchInfoVersion() == 2);
    __block bool suspended = false;
    __block const DylibPatch* cachePatch = patches;
    dyldCache->forEachPatchableExport(overriddenDylibIndex, ^(uint32_t dylibVMOffsetOfImpl, const char* exportName) {
        const DylibPatch* patch = cachePatch;
        ++cachePatch;
        dyldCache->forEachPatchableUseOfExportInImage(overriddenDylibIndex, dylibVMOffsetOfImpl, dylibToPatchIndex,
                                                      ^(uint32_t userVMOffset,
                                                        dyld3::MachOLoaded::PointerMetaData pmd, uint64_t addend) {
            // ensure dyld cache __DATA_CONST is writeable
            cacheDataConst.makeWriteable();

            // overridden dylib may not effect this dylib, so only suspend when we find it does effect it
            if ( !suspended ) {
                state.setVMAccountingSuspending(true);
                suspended = true;
            }
            uintptr_t  targetRuntimeAddress = patch->overrideOffsetOfImpl ? (uintptr_t)(this->loadAddress(state)) + ((intptr_t)patch->overrideOffsetOfImpl) : 0;
            uintptr_t* loc                  = (uintptr_t*)((uint8_t*)dylibToPatchMA + userVMOffset);
            uintptr_t  newValue             = targetRuntimeAddress + (uintptr_t)addend;
#if __has_feature(ptrauth_calls)
            if ( pmd.authenticated ) {
                newValue = dyld3::MachOLoaded::ChainedFixupPointerOnDisk::Arm64e::signPointer(newValue, loc, pmd.usesAddrDiversity, pmd.diversity, pmd.key);
                if ( *loc != newValue ) {
                    *loc = newValue;
                    if ( state.config.log.fixups ) {
                        state.log("cache fixup: *0x%012lX = 0x%012lX (*%s+0x%012lX = %s+0x%012lX) (JOP: diversity=0x%04X, addr-div=%d, key=%s)\n",
                                  (long)loc, newValue,
                                  dylibToPatch->leafName(), (long)userVMOffset,
                                  this->leafName(), (long)patch->overrideOffsetOfImpl,
                                  pmd.diversity, pmd.usesAddrDiversity, MachOLoaded::ChainedFixupPointerOnDisk::Arm64e::keyName(pmd.key));
                    }
                }
                return;
            }
#endif
            if ( *loc != newValue ) {
                *loc = newValue;
                if ( state.config.log.fixups )
                    state.log("cache fixup: *0x%012lX = 0x%012lX (*%s+0x%012lX = %s+0x%012lX)\n",
                              (long)loc, (long)newValue,
                              dylibToPatch->leafName(), (long)userVMOffset,
                              this->leafName(), (long)patch->overrideOffsetOfImpl);
            }
        });
    });
    // Ensure the end marker is as expected
    assert(cachePatch->overrideOffsetOfImpl == -1);

    if ( suspended )
        state.setVMAccountingSuspending(false);
}


void Loader::applyCachePatchesTo(RuntimeState& state, const Loader* dylibToPatch, DyldCacheDataConstLazyScopedWriter& cacheDataConst) const
{
    // do nothing if this dylib does not override something in the dyld cache
    uint16_t            overriddenDylibIndex;
    const DylibPatch*   patches;
    if ( !this->overridesDylibInCache(patches, overriddenDylibIndex) )
        return;
    if ( patches != nullptr )
        this->applyCachePatchesToOverride(state, dylibToPatch, overriddenDylibIndex, patches, cacheDataConst);

    // The override here may be a root of an iOSMac dylib, in which case we should also try patch uses of the macOS unzippered twin
    if ( !this->isPrebuilt && state.config.process.catalystRuntime ) {
        if ( const JustInTimeLoader* jitThis = this->isJustInTimeLoader() ) {
            if ( const DylibPatch* patches2 = jitThis->getCatalystMacTwinPatches() ) {
                uint16_t macOSTwinIndex = Loader::indexOfUnzipperedTwin(state, overriddenDylibIndex);
                if ( macOSTwinIndex != kNoUnzipperedTwin )
                    this->applyCachePatchesToOverride(state, dylibToPatch, macOSTwinIndex, patches2, cacheDataConst);
            }
        }
    }
}

#endif

uint16_t Loader::indexOfUnzipperedTwin(const RuntimeState& state, uint16_t overrideIndex)
{
    if ( state.config.process.catalystRuntime ) {
        // Find the macOS twin overridden index
        if ( const PrebuiltLoaderSet* cachePBLS = state.cachedDylibsPrebuiltLoaderSet() ) {
            const Loader* overridenDylibLdr = cachePBLS->atIndex(overrideIndex);
            if ( const PrebuiltLoader* overridenDylibPBLdr = overridenDylibLdr->isPrebuiltLoader() ) {
                if ( overridenDylibPBLdr->supportsCatalyst )
                    return overridenDylibPBLdr->indexOfTwin;
            }
        } else {
            // We might be running with an invalid version, so can't use Prebuilt loaders
            const char* catalystInstallName = state.config.dyldCache.addr->getIndexedImagePath(overrideIndex);
            if ( strncmp(catalystInstallName, "/System/iOSSupport/", 19) == 0 ) {
                const char* macTwinPath = &catalystInstallName[18];
                uint32_t macDylibCacheIndex;
                if ( state.config.dyldCache.indexOfPath(macTwinPath, macDylibCacheIndex) )
                    return macDylibCacheIndex;
            }
        }
    }

    return kNoUnzipperedTwin;
}

} // namespace
