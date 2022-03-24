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
#include <sys/types.h>

#include "Loader.h"
#include "PrebuiltLoader.h"
#include "JustInTimeLoader.h"
#include "MachOAnalyzer.h"
#include "BumpAllocator.h"
#include "DyldProcessConfig.h"
#include "DyldRuntimeState.h"
#include "OptimizerObjC.h"
#include "PerfectHash.h"
#include "PrebuiltObjC.h"
#include "objc-shared-cache.h"


using dyld3::MachOAnalyzer;
using dyld3::OverflowSafeArray;
typedef dyld4::PrebuiltObjC::ObjCOptimizerImage ObjCOptimizerImage;

namespace dyld4 {

////////////////////////////  ObjCStringTable ////////////////////////////////////////

uint32_t ObjCStringTable::hash(const char* key, size_t keylen) const
{
    uint64_t val   = objc::lookup8((uint8_t*)key, keylen, salt);
    uint32_t index = (uint32_t)((shift == 64) ? 0 : (val >> shift)) ^ scramble[tab[val & mask]];
    return index;
}

const char* ObjCStringTable::getString(const char* selName, RuntimeState& state) const
{
    std::optional<PrebuiltLoader::BindTargetRef> target = getPotentialTarget(selName);
    if ( !target.has_value() )
        return nullptr;

    const PrebuiltLoader::BindTargetRef& nameTarget = *target;
    const PrebuiltLoader::BindTargetRef  sentinel   = getSentinel();

    if ( memcmp(&nameTarget, &sentinel, sizeof(PrebuiltLoader::BindTargetRef)) == 0 )
        return nullptr;

    const char* stringValue = (const char*)target->value(state);
    if ( !strcmp(selName, stringValue) )
        return stringValue;
    return nullptr;
}

size_t ObjCStringTable::size(const objc::PerfectHash& phash)
{
    // Round tab[] to at least 8 in length to ensure the BindTarget's after are aligned
    uint32_t roundedTabSize        = std::max(phash.mask + 1, 8U);
    uint32_t roundedCheckBytesSize = std::max(phash.capacity, 8U);
    size_t   tableSize             = 0;
    tableSize += sizeof(ObjCStringTable);
    tableSize += roundedTabSize * sizeof(uint8_t);
    tableSize += roundedCheckBytesSize * sizeof(uint8_t);
    tableSize += phash.capacity * sizeof(PrebuiltLoader::BindTargetRef);
    return (size_t)align(tableSize, 3);
}

void ObjCStringTable::write(const objc::PerfectHash& phash, const Array<std::pair<const char*, PrebuiltLoader::BindTarget>>& strings)
{
    // Set header
    capacity              = phash.capacity;
    occupied              = phash.occupied;
    shift                 = phash.shift;
    mask                  = phash.mask;
    roundedTabSize        = std::max(phash.mask + 1, 8U);
    roundedCheckBytesSize = std::max(phash.capacity, 8U);
    salt                  = phash.salt;

    // Set hash data
    for ( uint32_t i = 0; i < 256; i++ ) {
        scramble[i] = phash.scramble[i];
    }
    for ( uint32_t i = 0; i < phash.mask + 1; i++ ) {
        tab[i] = phash.tab[i];
    }

    dyld3::Array<PrebuiltLoader::BindTargetRef> targetsArray    = targets();
    dyld3::Array<uint8_t>                       checkBytesArray = checkBytes();

    const PrebuiltLoader::BindTargetRef sentinel = getSentinel();

    // Set offsets to the sentinel
    for ( uint32_t i = 0; i < phash.capacity; i++ ) {
        targetsArray[i] = sentinel;
    }
    // Set checkbytes to 0
    for ( uint32_t i = 0; i < phash.capacity; i++ ) {
        checkBytesArray[i] = 0;
    }

    // Set real string offsets and checkbytes
    for ( const auto& s : strings ) {
        assert(memcmp(&s.second, &sentinel, sizeof(PrebuiltLoader::BindTargetRef)) != 0);
        uint32_t h         = hash(s.first);
        targetsArray[h]    = s.second;
        checkBytesArray[h] = checkbyte(s.first);
    }
}

////////////////////////////  ObjCSelectorOpt ////////////////////////////////////////
const char* ObjCSelectorOpt::getStringAtIndex(uint32_t index, RuntimeState& state) const
{
    if ( index >= capacity )
        return nullptr;

    PrebuiltLoader::BindTargetRef       target   = targets()[index];
    const PrebuiltLoader::BindTargetRef sentinel = getSentinel();
    if ( memcmp(&target, &sentinel, sizeof(PrebuiltLoader::BindTargetRef)) == 0 )
        return nullptr;

    const char* stringValue = (const char*)target.value(state);
    return stringValue;
}

void ObjCSelectorOpt::forEachString(void (^callback)(const PrebuiltLoader::BindTargetRef& target)) const
{
    const PrebuiltLoader::BindTargetRef sentinel = getSentinel();

    dyld3::Array<PrebuiltLoader::BindTargetRef> stringTargets = targets();
    for ( const PrebuiltLoader::BindTargetRef& target : stringTargets ) {
        if ( memcmp(&target, &sentinel, sizeof(PrebuiltLoader::BindTargetRef)) == 0 )
            continue;
        callback(target);
    }
}

////////////////////////////  ObjCClassOpt ////////////////////////////////////////

// Returns true if the class was found and the callback said to stop
bool ObjCClassOpt::forEachClass(const char* className, RuntimeState& state,
                                void (^callback)(void* classPtr, bool isLoaded, bool* stop)) const
{
    uint32_t index = getIndex(className);
    if ( index == ObjCStringTable::indexNotFound )
        return false;

    const PrebuiltLoader::BindTargetRef sentinel = getSentinel();

    const PrebuiltLoader::BindTargetRef& nameTarget = targets()[index];
    if ( memcmp(&nameTarget, &sentinel, sizeof(PrebuiltLoader::BindTargetRef)) == 0 )
        return false;

    const char* nameStringValue = (const char*)nameTarget.value(state);
    if ( strcmp(className, nameStringValue) != 0 )
        return false;

    // The name matched so now call the handler on all the classes for this name
    const Array<PrebuiltLoader::BindTargetRef> classes    = classTargets();
    const Array<PrebuiltLoader::BindTargetRef> duplicates = duplicateTargets();

    const PrebuiltLoader::BindTargetRef& classTarget = classes[index];
    if ( !classTarget.isAbsolute() ) {
        // A regular target points to the single class implementation
        // This class has a single implementation
        void* classImpl = (void*)classTarget.value(state);
        bool  stop      = false;
        callback(classImpl, true, &stop);
        return stop;
    }
    else {
        // This class has mulitple implementations.
        // The absolute value of the class target is the index in to the duplicates table
        // The first entry we point to is the count of duplicates for this class
        size_t                              duplicateStartIndex  = (size_t)classTarget.value(state);
        const PrebuiltLoader::BindTargetRef duplicateCountTarget = duplicates[duplicateStartIndex];
        ++duplicateStartIndex;
        assert(duplicateCountTarget.isAbsolute());
        uint64_t duplicateCount = duplicateCountTarget.value(state);

        for ( size_t dupeIndex = 0; dupeIndex != duplicateCount; ++dupeIndex ) {
            const PrebuiltLoader::BindTargetRef& duplicateTarget = duplicates[duplicateStartIndex + dupeIndex];

            void* classImpl = (void*)duplicateTarget.value(state);
            bool  stop      = false;
            callback(classImpl, true, &stop);
            if ( stop )
                return true;
        }
    }
    return false;
}

void ObjCClassOpt::forEachClass(RuntimeState& state,
                                void (^callback)(const PrebuiltLoader::BindTargetRef&        nameTarget,
                                                 const Array<PrebuiltLoader::BindTargetRef>& implTargets)) const
{

    const PrebuiltLoader::BindTargetRef sentinel = getSentinel();

    Array<PrebuiltLoader::BindTargetRef> stringTargets = targets();
    Array<PrebuiltLoader::BindTargetRef> classes       = classTargets();
    Array<PrebuiltLoader::BindTargetRef> duplicates    = duplicateTargets();
    for ( unsigned i = 0; i != capacity; ++i ) {
        const PrebuiltLoader::BindTargetRef& nameTarget = stringTargets[i];
        if ( memcmp(&nameTarget, &sentinel, sizeof(PrebuiltLoader::BindTargetRef)) == 0 )
            continue;

        // Walk each class for this key
        PrebuiltLoader::BindTargetRef classTarget = classes[i];
        if ( !classTarget.isAbsolute() ) {
            // A regular target points to the single class implementation
            // This class has a single implementation
            const Array<PrebuiltLoader::BindTargetRef> implTarget(&classTarget, 1, 1);
            callback(nameTarget, implTarget);
        }
        else {
            // This class has mulitple implementations.
            // The absolute value of the class target is the index in to the duplicates table
            // The first entry we point to is the count of duplicates for this class
            uintptr_t                           duplicateStartIndex  = (uintptr_t)classTarget.value(state);
            const PrebuiltLoader::BindTargetRef duplicateCountTarget = duplicates[duplicateStartIndex];
            ++duplicateStartIndex;
            assert(duplicateCountTarget.isAbsolute());
            uintptr_t duplicateCount = (uintptr_t)duplicateCountTarget.value(state);

            callback(nameTarget, duplicates.subArray(duplicateStartIndex, duplicateCount));
        }
    }
}

size_t ObjCClassOpt::size(const objc::PerfectHash& phash, uint32_t numClassesWithDuplicates,
                          uint32_t totalDuplicates)
{
    size_t tableSize = 0;
    tableSize += ObjCStringTable::size(phash);
    tableSize += phash.capacity * sizeof(PrebuiltLoader::BindTargetRef);                               // classTargets
    tableSize += sizeof(uint32_t);                                                                     // duplicateCount
    tableSize += (numClassesWithDuplicates + totalDuplicates) * sizeof(PrebuiltLoader::BindTargetRef); // duplicateTargets
    return (size_t)align(tableSize, 3);
}

void ObjCClassOpt::write(const objc::PerfectHash& phash, const Array<std::pair<const char*, PrebuiltLoader::BindTarget>>& strings,
                         const dyld3::CStringMultiMapTo<PrebuiltLoader::BindTarget>& classes,
                         uint32_t numClassesWithDuplicates, uint32_t totalDuplicates)
{
    ObjCStringTable::write(phash, strings);
    duplicateCount() = numClassesWithDuplicates + totalDuplicates;

    __block dyld3::Array<PrebuiltLoader::BindTargetRef> classTargets     = this->classTargets();
    __block dyld3::Array<PrebuiltLoader::BindTargetRef> duplicateTargets = this->duplicateTargets();

    const PrebuiltLoader::BindTargetRef sentinel = getSentinel();

    // Set class offsets to 0
    for ( uint32_t i = 0; i < capacity; i++ ) {
        classTargets[i] = sentinel;
    }

    // Empty the duplicate targets array so that we can push elements in to it.  It already has the correct capacity
    duplicateTargets.resize(0);

    classes.forEachEntry(^(const char* const& key, const PrebuiltLoader::BindTarget** values, uint64_t valuesCount) {
        uint32_t keyIndex = getIndex(key);
        assert(keyIndex != indexNotFound);
        assert(memcmp(&classTargets[keyIndex], &sentinel, sizeof(PrebuiltLoader::BindTargetRef)) == 0);

        if ( valuesCount == 1 ) {
            // Only one entry so write it in to the class offsets directly
            const PrebuiltLoader::BindTarget& classTarget = *(values[0]);
            classTargets[keyIndex]                        = PrebuiltLoader::BindTargetRef(classTarget);
            return;
        }

        // We have more than one value.  We add a placeholder to the class offsets which tells us the head
        // of the linked list of classes in the duplicates array

        PrebuiltLoader::BindTargetRef classTargetPlaceholder = PrebuiltLoader::BindTargetRef::makeAbsolute(duplicateTargets.count());
        classTargets[keyIndex]                               = classTargetPlaceholder;

        // The first value we push in to the duplicates array for this class is the count
        // of how many duplicates for this class we have
        duplicateTargets.push_back(PrebuiltLoader::BindTargetRef::makeAbsolute(valuesCount));
        for ( uint64_t i = 0; i != valuesCount; ++i ) {
            PrebuiltLoader::BindTarget classTarget = *(values[i]);
            duplicateTargets.push_back(PrebuiltLoader::BindTargetRef(classTarget));
        }
    });

    assert(duplicateTargets.count() == duplicateCount());
}

//////////////////////// ObjCOptimizerImage /////////////////////////////////

ObjCOptimizerImage::ObjCOptimizerImage(const JustInTimeLoader* jitLoader, uint64_t loadAddress, uint32_t pointerSize)
    : jitLoader(jitLoader)
    , pointerSize(pointerSize)
    , loadAddress(loadAddress)
{
}

#if BUILDING_CACHE_BUILDER || BUILDING_CLOSURE_UTIL
void ObjCOptimizerImage::calculateMissingWeakImports(RuntimeState& state)
{
    const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)jitLoader->loadAddress(state);

    // build targets table
    STACK_ALLOC_OVERFLOW_SAFE_ARRAY(bool, bindTargetsAreWeakImports, 512);
    STACK_ALLOC_OVERFLOW_SAFE_ARRAY(bool, overrideBindTargetsAreWeakImports, 16);
    __block bool                           foundMissingWeakImport = false;
    bool                                   allowLazyBinds         = false;
    JustInTimeLoader::CacheWeakDefOverride cacheWeakDefFixup      = ^(uint32_t cachedDylibIndex, uint32_t cachedDylibVMOffset, const JustInTimeLoader::ResolvedSymbol& target) {};
    jitLoader->forEachBindTarget(diag, state, cacheWeakDefFixup, allowLazyBinds, ^(const JustInTimeLoader::ResolvedSymbol& target, bool& stop) {
        if ( (target.kind == Loader::ResolvedSymbol::Kind::bindAbsolute) && (target.targetRuntimeOffset == 0) ) {
            foundMissingWeakImport = true;
            bindTargetsAreWeakImports.push_back(true);
        }
        else {
            bindTargetsAreWeakImports.push_back(false);
        }
    }, ^(const JustInTimeLoader::ResolvedSymbol& target, bool& stop) {
        if ( (target.kind == Loader::ResolvedSymbol::Kind::bindAbsolute) && (target.targetRuntimeOffset == 0) ) {
            foundMissingWeakImport = true;
            overrideBindTargetsAreWeakImports.push_back(true);
        }
        else {
            overrideBindTargetsAreWeakImports.push_back(false);
        }
    });
    if ( diag.hasError() )
        return;

    if ( foundMissingWeakImport ) {
        if ( ma->hasChainedFixups() ) {
            // walk all chains
            ma->withChainStarts(diag, ma->chainStartsOffset(), ^(const dyld_chained_starts_in_image* startsInfo) {
                ma->forEachFixupInAllChains(diag, startsInfo, false, ^(MachOLoaded::ChainedFixupPointerOnDisk* fixupLoc, const dyld_chained_starts_in_segment* segInfo, bool& fixupsStop) {
                    uint64_t fixupOffset = (uint8_t*)fixupLoc - (uint8_t*)ma;
                    uint32_t bindOrdinal;
                    int64_t  addend;
                    if ( fixupLoc->isBind(segInfo->pointer_format, bindOrdinal, addend) ) {
                        if ( bindOrdinal < bindTargetsAreWeakImports.count() ) {
                            if ( bindTargetsAreWeakImports[bindOrdinal] )
                                missingWeakImportOffsets[fixupOffset] = true;
                        }
                        else {
                            diag.error("out of range bind ordinal %d (max %lu)", bindOrdinal, bindTargetsAreWeakImports.count());
                            fixupsStop = true;
                        }
                    }
                });
            });
            if ( diag.hasError() )
                return;
        }
        else if ( ma->hasOpcodeFixups() ) {
            // process all bind opcodes
            ma->forEachBindLocation_Opcodes(diag, ^(uint64_t runtimeOffset, unsigned targetIndex, bool& fixupsStop) {
                if ( targetIndex < bindTargetsAreWeakImports.count() ) {
                    if ( bindTargetsAreWeakImports[targetIndex] )
                        missingWeakImportOffsets[runtimeOffset] = true;
                }
                else {
                    diag.error("out of range bind ordinal %d (max %lu)", targetIndex, bindTargetsAreWeakImports.count());
                    fixupsStop = true;
                }
            }, ^(uint64_t runtimeOffset, unsigned overrideBindTargetIndex, bool& fixupsStop) {
                if ( overrideBindTargetIndex < overrideBindTargetsAreWeakImports.count() ) {
                    if ( overrideBindTargetsAreWeakImports[overrideBindTargetIndex] )
                        missingWeakImportOffsets[runtimeOffset] = true;
                }
                else {
                    diag.error("out of range bind ordinal %d (max %lu)", overrideBindTargetIndex, overrideBindTargetsAreWeakImports.count());
                    fixupsStop = true;
                }
            });
            if ( diag.hasError() )
                return;
        }
        else {
            // process external relocations
            ma->forEachBindLocation_Relocations(diag, ^(uint64_t runtimeOffset, unsigned targetIndex, bool& fixupsStop) {
                if ( targetIndex < bindTargetsAreWeakImports.count() ) {
                    if ( bindTargetsAreWeakImports[targetIndex] )
                        missingWeakImportOffsets[runtimeOffset] = true;
                }
                else {
                    diag.error("out of range bind ordinal %d (max %lu)", targetIndex, bindTargetsAreWeakImports.count());
                    fixupsStop = true;
                }
            });
            if ( diag.hasError() )
                return;
        }
    }
}
#endif // (BUILDING_CACHE_BUILDER || BUILDING_CLOSURE_UTIL)

bool ObjCOptimizerImage::isNull(uint64_t vmAddr, const dyld3::MachOAnalyzer* ma, intptr_t slide) const
{
#if BUILDING_CACHE_BUILDER || BUILDING_CLOSURE_UTIL
    uint64_t runtimeOffset = vmAddr - loadAddress;
    return (missingWeakImportOffsets.find(runtimeOffset) != missingWeakImportOffsets.end());
#elif BUILDING_DYLD
    // In dyld, we are live, so we can just check if we point to a null value
    uintptr_t* pointer = (uintptr_t*)(vmAddr + slide);
    return (*pointer == 0);
#else
    // FIXME: Have we been slide or not in the non-dyld case?
    assert(0);
    return false;
#endif
}

void ObjCOptimizerImage::visitReferenceToObjCSelector(const objc::SelectorHashTable* objcSelOpt,
                                                      const PrebuiltObjC::SelectorMapTy& appSelectorMap,
                                                      uint64_t selectorReferenceRuntimeOffset, uint64_t selectorStringRuntimeOffset,
                                                      const char* selectorString)
{

    // fprintf(stderr, "selector: %p -> %p %s\n", (void*)selectorReferenceRuntimeOffset, (void*)selectorStringRuntimeOffset, selectorString);
    if ( std::optional<uint32_t> cacheSelectorIndex = objcSelOpt->tryGetIndex(selectorString) ) {
        // We got the selector from the cache so add a fixup to point there.
        // We use an absolute bind here, to reference the index in to the shared cache table
        PrebuiltLoader::BindTargetRef bindTarget = PrebuiltLoader::BindTargetRef::makeAbsolute(*cacheSelectorIndex);

        //printf("Overriding fixup at 0x%08llX to cache offset 0x%08llX\n", selectorUseImageOffset, (uint64_t)objcSelOpt->getEntryForIndex(cacheSelectorIndex) - (uint64_t)state.config.dyldCache());
        selectorFixups.push_back(bindTarget);
        return;
    }

    // See if this selector is already in the app map from a previous image
    auto appSelectorIt = appSelectorMap.find(selectorString);
    if ( appSelectorIt != appSelectorMap.end() ) {
        // This selector was found in a previous image, so use it here.

        //printf("Overriding fixup at 0x%08llX to other image\n", selectorUseImageOffset);
        selectorFixups.push_back(PrebuiltLoader::BindTargetRef(appSelectorIt->second));
        return;
    }

    // See if this selector is already in the map for this image
    auto itAndInserted = selectorMap.insert({ selectorString, Loader::BindTarget() });
    if ( itAndInserted.second ) {
        // We added the selector so its pointing in to our own image.
        Loader::BindTarget target;
        target.loader               = jitLoader;
        target.runtimeOffset        = selectorStringRuntimeOffset;
        itAndInserted.first->second = target;

        // We'll add a fixup anyway as we want a sel ref fixup for every entry in the sel refs section

        //printf("Fixup at 0x%08llX to '%s' offset 0x%08llX\n", selectorUseImageOffset, findLoadedImage(target.image.imageNum).path(), target.image.offset);
        selectorFixups.push_back(PrebuiltLoader::BindTargetRef(target));
        return;
    }

    // This selector was found elsewhere in our image.  As we want a fixup for every selref, we'll
    // add one here too
    Loader::BindTarget& target = itAndInserted.first->second;

    //printf("Overriding fixup at 0x%08llX to '%s' offset 0x%08llX\n", selectorUseImageOffset, findLoadedImage(target.image.imageNum).path(), target.image.offset);
    selectorFixups.push_back(PrebuiltLoader::BindTargetRef(target));
}

// Check if the given class is in an image loaded in the shared cache.
// If so, add the class to the duplicate map
static void checkForDuplicateClass(const void* dyldCacheBase,
                                   const char* className, const objc::ClassHashTable* objcClassOpt,
                                   const PrebuiltObjC::SharedCacheImagesMapTy& sharedCacheImagesMap,
                                   const PrebuiltObjC::DuplicateClassesMapTy& duplicateSharedCacheClasses,
                                   ObjCOptimizerImage& image)
{
    objcClassOpt->forEachClass(className,
                               ^(uint64_t classCacheOffset, uint16_t dylibObjCIndex, bool &stopObjects) {
        // Check if this image is loaded
        if ( auto cacheIt = sharedCacheImagesMap.find(dylibObjCIndex); cacheIt != sharedCacheImagesMap.end() ) {
            const Loader* ldr = cacheIt->second.second;

            // We have a duplicate class, so check if we've already got it in our map.
            if ( duplicateSharedCacheClasses.find(className) == duplicateSharedCacheClasses.end() ) {
                // We haven't seen this one yet, so record it in the map for this image
                const dyld3::MachOLoaded* sharedCacheMH = cacheIt->second.first;
                uint64_t           classPointer = (uint64_t)dyldCacheBase + classCacheOffset;
                uint64_t           classVMOffset = classPointer - (uint64_t)sharedCacheMH;
                Loader::BindTarget classTarget   = { ldr, classVMOffset };
                image.duplicateSharedCacheClassMap.insert({ className, classTarget });
            }

            stopObjects = true;
        }
    });
}

void ObjCOptimizerImage::visitClass(const void* dyldCacheBase,
                                    const objc::ClassHashTable* objcClassOpt,
                                    const SharedCacheImagesMapTy& sharedCacheImagesMap,
                                    const DuplicateClassesMapTy& duplicateSharedCacheClasses,
                                    uint64_t classVMAddr, uint64_t classNameVMAddr, const char* className)
{

    // If the class also exists in a shared cache image which is loaded, then objc
    // would have found that one, regardless of load order.
    // In that case, we still add this class to the map, but also track which shared cache class it is a duplicate of
    checkForDuplicateClass(dyldCacheBase, className, objcClassOpt, sharedCacheImagesMap, duplicateSharedCacheClasses, *this);

    uint64_t classNameVMOffset   = classNameVMAddr - loadAddress;
    uint64_t classObjectVMOffset = classVMAddr - loadAddress;
    classLocations.push_back({ className, classNameVMOffset, classObjectVMOffset });
}

static bool protocolIsInSharedCache(const char* protocolName,
                                    const objc::ProtocolHashTable* objcProtocolOpt,
                                    const PrebuiltObjC::SharedCacheImagesMapTy& sharedCacheImagesMap)
{
    __block bool foundProtocol = false;
    objcProtocolOpt->forEachProtocol(protocolName,
                                     ^(uint64_t classCacheOffset, uint16_t dylibObjCIndex, bool &stopObjects) {
        // Check if this image is loaded
        if ( auto cacheIt = sharedCacheImagesMap.find(dylibObjCIndex); cacheIt != sharedCacheImagesMap.end() ) {
            foundProtocol = true;
            stopObjects = true;
        }
    });
    return foundProtocol;
}

void ObjCOptimizerImage::visitProtocol(const objc::ProtocolHashTable* objcProtocolOpt,
                                       const SharedCacheImagesMapTy& sharedCacheImagesMap,
                                       uint64_t protocolVMAddr, uint64_t protocolNameVMAddr, const char* protocolName)
{

    uint32_t protocolIndex = (uint32_t)protocolISAFixups.count();
    protocolISAFixups.push_back(false);

    // If the protocol also exists in a shared cache image which is loaded, then objc
    // would have found that one, regardless of load order.  So we can just skip this one.
    if ( protocolIsInSharedCache(protocolName, objcProtocolOpt, sharedCacheImagesMap) )
        return;

    uint64_t protocolNameVMOffset   = protocolNameVMAddr - loadAddress;
    uint64_t protocolObjectVMOffset = protocolVMAddr - loadAddress;
    protocolLocations.push_back({ protocolName, protocolNameVMOffset, protocolObjectVMOffset });

    // Record which index this protocol uses in protocolISAFixups.  Later we can change its entry if we
    // choose this protocol as the canonical definition.
    protocolIndexMap[protocolObjectVMOffset] = protocolIndex;
}

//////////////////////// ObjC Optimisations /////////////////////////////////

// HACK!: dyld3 used to know if each image in a closure has been rebased or not when it was building the closure
// Now we try to make good guesses based on whether its the shared cache or not, and which binary is executing this code
static bool hasBeenRebased(const Loader* ldr)
{
#if BUILDING_DYLD
    // In dyld, we always run this analysis after everything has already been fixed up
    return true;
#elif BUILDING_CLOSURE_UTIL
    // dyld_closure_util assumes that on disk binaries haven't had fixups applied
    return false;
#else
    // In the shared cache builder, nothing has been rebased yet
    return false;
#endif
}

static void optimizeObjCSelectors(RuntimeState& state,
                                  const objc::SelectorHashTable* objcSelOpt,
                                  const PrebuiltObjC::SelectorMapTy& appSelectorMap,
                                  ObjCOptimizerImage&                image)
{

    const dyld3::MachOAnalyzer*                 ma              = (const dyld3::MachOAnalyzer*)image.jitLoader->loadAddress(state);
    uint32_t                                    pointerSize     = ma->pointerSize();
    const dyld3::MachOAnalyzer::VMAddrConverter vmAddrConverter = ma->makeVMAddrConverter(hasBeenRebased(image.jitLoader));

    // The legacy (objc1) codebase uses a bunch of sections we don't want to reason about.  If we see them just give up.
    __block bool foundBadSection = false;
    ma->forEachSection(^(const MachOAnalyzer::SectionInfo& sectInfo, bool malformedSectionRange, bool& stop) {
        if ( strcmp(sectInfo.segInfo.segName, "__OBJC") != 0 )
            return;
        if ( strcmp(sectInfo.sectName, "__module_info") == 0 ) {
            foundBadSection = true;
            stop            = true;
            return;
        }
        if ( strcmp(sectInfo.sectName, "__protocol") == 0 ) {
            foundBadSection = true;
            stop            = true;
            return;
        }
        if ( strcmp(sectInfo.sectName, "__message_refs") == 0 ) {
            foundBadSection = true;
            stop            = true;
            return;
        }
    });
    if ( foundBadSection ) {
        image.diag.error("Old objc section");
        return;
    }

    // Visit the message refs
    // Note this isn't actually supported in libobjc any more.  Its logic for deciding whether to support it is if this is true:
    // #if (defined(__x86_64__) && (TARGET_OS_OSX || TARGET_OS_SIMULATOR))
    // So to keep it simple, lets only do this walk if we are x86_64
    if ( ma->isArch("x86_64") || ma->isArch("x86_64h") ) {
        if ( ma->hasObjCMessageReferences() ) {
            image.diag.error("Cannot handle message refs");
            return;
        }
    }

    // We only record selector references for __objc_selrefs and pointer based method lists.  If we find a relative method list pointing
    // outside of __objc_selrefs then we give up for now
    uint64_t selRefsStartRuntimeOffset = image.binaryInfo.selRefsRuntimeOffset;
    uint64_t selRefsEndRuntimeOffset   = selRefsStartRuntimeOffset + (pointerSize * image.binaryInfo.selRefsCount);
    auto     visitMethod               = ^(uint64_t methodVMAddr, const dyld3::MachOAnalyzer::ObjCMethod& method, bool& stop) {
        uint64_t selectorReferenceRuntimeOffset = method.nameLocationVMAddr - image.loadAddress;
        if ( (selectorReferenceRuntimeOffset < selRefsStartRuntimeOffset) || (selectorReferenceRuntimeOffset >= selRefsEndRuntimeOffset) ) {
            image.diag.error("Cannot handle relative method list pointing outside of __objc_selrefs");
            stop = true;
        }
    };

    auto visitMethodList = ^(uint64_t methodListVMAddr, bool& hasPointerBasedMethodList, bool& hasRelativeMethodList) {
        if ( methodListVMAddr == 0 )
            return;
        uint64_t methodListRuntimeOffset = methodListVMAddr - image.loadAddress;
        if ( ma->objcMethodListIsRelative(methodListRuntimeOffset) ) {
            // Check relative method lists
            ma->forEachObjCMethod(methodListVMAddr, vmAddrConverter, 0, visitMethod);
        }
        else {
            // Record if we found a pointer based method list.  This lets us skip walking method lists later if
            // they are all relative method lists
            hasPointerBasedMethodList = true;
        }
    };

    if ( image.binaryInfo.classListCount != 0 ) {
        __block bool hasPointerBasedMethodList = false;
        __block bool hasRelativeMethodList     = false;
        auto         visitClass                = ^(uint64_t classVMAddr, uint64_t classSuperclassVMAddr, uint64_t classDataVMAddr,
                            const dyld3::MachOAnalyzer::ObjCClassInfo& objcClass, bool isMetaClass, bool& stop) {
            visitMethodList(objcClass.baseMethodsVMAddr(pointerSize), hasPointerBasedMethodList, hasRelativeMethodList);
            if ( image.diag.hasError() )
                stop = true;
        };
        ma->forEachObjCClass(image.binaryInfo.classListRuntimeOffset, image.binaryInfo.classListCount, vmAddrConverter, visitClass);
        if ( image.diag.hasError() )
            return;

        image.binaryInfo.hasClassMethodListsToUnique     = hasPointerBasedMethodList;
        image.binaryInfo.hasClassMethodListsToSetUniqued = hasPointerBasedMethodList;
    }

    if ( image.binaryInfo.categoryCount != 0 ) {
        __block bool hasPointerBasedMethodList = false;
        __block bool hasRelativeMethodList     = false;
        auto         visitCategory             = ^(uint64_t categoryVMAddr, const dyld3::MachOAnalyzer::ObjCCategory& objcCategory, bool& stop) {
            visitMethodList(objcCategory.instanceMethodsVMAddr, hasPointerBasedMethodList, hasRelativeMethodList);
            if ( image.diag.hasError() ) {
                stop = true;
                return;
            }
            visitMethodList(objcCategory.classMethodsVMAddr, hasPointerBasedMethodList, hasRelativeMethodList);
            if ( image.diag.hasError() )
                stop = true;
        };
        ma->forEachObjCCategory(image.binaryInfo.categoryListRuntimeOffset, image.binaryInfo.categoryCount, vmAddrConverter, visitCategory);
        if ( image.diag.hasError() )
            return;

        image.binaryInfo.hasCategoryMethodListsToUnique     = hasPointerBasedMethodList;
        image.binaryInfo.hasCategoryMethodListsToSetUniqued = hasPointerBasedMethodList;
    }

    if ( image.binaryInfo.protocolListCount != 0 ) {
        __block bool hasPointerBasedMethodList = false;
        __block bool hasRelativeMethodList     = false;
        auto         visitProtocol             = ^(uint64_t protocolVMAddr, const dyld3::MachOAnalyzer::ObjCProtocol& objCProtocol, bool& stop) {
            visitMethodList(objCProtocol.instanceMethodsVMAddr, hasPointerBasedMethodList, hasRelativeMethodList);
            if ( image.diag.hasError() ) {
                stop = true;
                return;
            }
            visitMethodList(objCProtocol.classMethodsVMAddr, hasPointerBasedMethodList, hasRelativeMethodList);
            if ( image.diag.hasError() ) {
                stop = true;
                return;
            }
            visitMethodList(objCProtocol.optionalInstanceMethodsVMAddr, hasPointerBasedMethodList, hasRelativeMethodList);
            if ( image.diag.hasError() ) {
                stop = true;
                return;
            }
            visitMethodList(objCProtocol.optionalClassMethodsVMAddr, hasPointerBasedMethodList, hasRelativeMethodList);
            if ( image.diag.hasError() )
                stop = true;
        };
        ma->forEachObjCProtocol(image.binaryInfo.protocolListRuntimeOffset, image.binaryInfo.protocolListCount, vmAddrConverter, visitProtocol);
        if ( image.diag.hasError() )
            return;

        image.binaryInfo.hasProtocolMethodListsToUnique     = hasPointerBasedMethodList;
        image.binaryInfo.hasProtocolMethodListsToSetUniqued = hasPointerBasedMethodList;
    }

    PrebuiltObjC::forEachSelectorReferenceToUnique(state, ma, image.loadAddress, image.binaryInfo, vmAddrConverter,
                                                   ^(uint64_t selectorReferenceRuntimeOffset, uint64_t selectorStringRuntimeOffset) {
                                                       // Note we don't check if the string is printable.  We already checked earlier that this image doesn't have
                                                       // Fairplay or protected segments, which would prevent seeing the strings.
                                                       const char* selectorString = (const char*)ma + selectorStringRuntimeOffset;
                                                       image.visitReferenceToObjCSelector(objcSelOpt, appSelectorMap, selectorReferenceRuntimeOffset, selectorStringRuntimeOffset, selectorString);
                                                   });
}

static void optimizeObjCClasses(RuntimeState& state,
                                const objc::ClassHashTable* objcClassOpt,
                                const PrebuiltObjC::SharedCacheImagesMapTy& sharedCacheImagesMap,
                                const PrebuiltObjC::DuplicateClassesMapTy& duplicateSharedCacheClasses,
                                ObjCOptimizerImage& image)
{
    if ( image.binaryInfo.classListCount == 0 )
        return;

    const dyld3::MachOAnalyzer*                 ma              = (const dyld3::MachOAnalyzer*)image.jitLoader->loadAddress(state);
    const intptr_t                              slide           = ma->getSlide();
    const dyld3::MachOAnalyzer::VMAddrConverter vmAddrConverter = ma->makeVMAddrConverter(hasBeenRebased(image.jitLoader));

#if BUILDING_CACHE_BUILDER || BUILDING_CLOSURE_UTIL
    image.calculateMissingWeakImports(state);
    if ( image.diag.hasError() )
        return;
#endif

    dyld3::MachOAnalyzer::ClassCallback visitClass = ^(uint64_t classVMAddr,
                                                       uint64_t classSuperclassVMAddr, uint64_t classDataVMAddr,
                                                       const MachOAnalyzer::ObjCClassInfo& objcClass, bool isMetaClass,
                                                       bool& stop) {
        if ( isMetaClass )
            return;

        // Make sure the superclass pointer is not nil.  Unless we are a root class as those don't have a superclass
        if ( image.isNull(classSuperclassVMAddr, ma, slide) ) {
            const uint32_t RO_ROOT = (1 << 1);
            if ( (objcClass.flags(image.pointerSize) & RO_ROOT) == 0 ) {
                uint64_t classNameVMAddr = objcClass.nameVMAddr(image.pointerSize);
                const char* className = (const char*)(classNameVMAddr + slide);
                image.diag.error("Missing weak superclass of class %s in %s", className, image.jitLoader->path());
                return;
            }
        }

        // Does this class need to be fixed up for stable Swift ABI.
        // Note the order matches the objc runtime in that we always do this fix before checking for dupes,
        // but after excluding classes with missing weak superclasses.
        if ( objcClass.isUnfixedBackwardDeployingStableSwift() ) {
            // Class really is stable Swift, pretending to be pre-stable.
            image.binaryInfo.hasClassStableSwiftFixups = true;
        }

        uint64_t classNameVMAddr = objcClass.nameVMAddr(image.pointerSize);
        // Note we don't check if the string is printable.  We already checked earlier that this image doesn't have
        // Fairplay or protected segments, which would prevent seeing the strings.
        const char* className = (const char*)(classNameVMAddr + slide);

        image.visitClass(state.config.dyldCache.addr, objcClassOpt, sharedCacheImagesMap, duplicateSharedCacheClasses, classVMAddr, classNameVMAddr, className);
    };

    ma->forEachObjCClass(image.binaryInfo.classListRuntimeOffset, image.binaryInfo.classListCount, vmAddrConverter, visitClass);
}

static void optimizeObjCProtocols(RuntimeState& state,
                                  const objc::ProtocolHashTable* objcProtocolOpt,
                                  const PrebuiltObjC::SharedCacheImagesMapTy& sharedCacheImagesMap,
                                  ObjCOptimizerImage& image)
{
    if ( image.binaryInfo.protocolListCount == 0 )
        return;

    const dyld3::MachOAnalyzer*                 ma              = (const dyld3::MachOAnalyzer*)image.jitLoader->loadAddress(state);
    const intptr_t                              slide           = ma->getSlide();
    const dyld3::MachOAnalyzer::VMAddrConverter vmAddrConverter = ma->makeVMAddrConverter(hasBeenRebased(image.jitLoader));

    image.protocolISAFixups.reserve(image.binaryInfo.protocolListCount);

    dyld3::MachOAnalyzer::ProtocolCallback visitProtocol = ^(uint64_t                                  protocolVMAddr,
                                                             const dyld3::MachOAnalyzer::ObjCProtocol& objCProtocol,
                                                             bool&                                     stop) {
        if ( objCProtocol.isaVMAddr != 0 ) {
            // We can't optimize this protocol if it has an ISA as we want to override it
            image.diag.error("Protocol ISA must be null");
            stop = true;
            return;
        }

        uint64_t protocolNameVMAddr = objCProtocol.nameVMAddr;
        // Note we don't check if the string is printable.  We already checked earlier that this image doesn't have
        // Fairplay or protected segments, which would prevent seeing the strings.
        const char* protocolName = (const char*)(protocolNameVMAddr + slide);

        image.visitProtocol(objcProtocolOpt, sharedCacheImagesMap, protocolVMAddr, protocolNameVMAddr, protocolName);
    };

    ma->forEachObjCProtocol(image.binaryInfo.protocolListRuntimeOffset, image.binaryInfo.protocolListCount, vmAddrConverter, visitProtocol);
}

static void
writeClassOrProtocolHashTable(RuntimeState& state, bool classes,
                              Array<ObjCOptimizerImage>& objcImages,
                              OverflowSafeArray<uint8_t>& hashTable,
                              const PrebuiltObjC::DuplicateClassesMapTy& duplicateSharedCacheClassMap)
{

    dyld3::CStringMultiMapTo<PrebuiltLoader::BindTarget> seenObjectsMap;
    dyld3::CStringMapTo<PrebuiltLoader::BindTarget>      objectNameMap;
    OverflowSafeArray<const char*>                       objectNames;

    // Note we walk the images backwards as we want them in load order to match the order they are registered with objc
    for ( size_t imageIndex = 0, reverseIndex = (objcImages.count() - 1); imageIndex != objcImages.count(); ++imageIndex, --reverseIndex ) {
        if ( objcImages[reverseIndex].diag.hasError() )
            continue;
        ObjCOptimizerImage& image = objcImages[reverseIndex];

        const OverflowSafeArray<ObjCOptimizerImage::ObjCObject>& objectLocations = classes ? image.classLocations : image.protocolLocations;

        for ( const ObjCOptimizerImage::ObjCObject& objectLocation : objectLocations ) {
            //uint64_t nameVMAddr     = ma->preferredLoadAddress() + classImage.offsetOfClassNames + classNameTarget.classNameImageOffset;
            //printf("%s: 0x%08llx = '%s'\n", li.path(), nameVMAddr, className);

            // Also track the name
            PrebuiltLoader::BindTarget nameTarget    = { image.jitLoader, objectLocation.nameRuntimeOffset };
            auto                       itAndInserted = objectNameMap.insert({ objectLocation.name, nameTarget });
            if ( itAndInserted.second ) {
                // We inserted the class name so we need to add it to the strings for the closure hash table
                objectNames.push_back(objectLocation.name);

                // If we are processing protocols, and this is the first one we've seen, then track its ISA to be fixed up
                if ( !classes ) {
                    auto protocolIndexIt = image.protocolIndexMap.find(objectLocation.valueRuntimeOffset);
                    assert(protocolIndexIt != image.protocolIndexMap.end());
                    image.protocolISAFixups[protocolIndexIt->second] = true;
                }

                // Check if we have a duplicate.  If we do, it will be on the last image which had a duplicate class name,
                // but as we walk images backwards, we'll see this before all other images with duplicates.
                // Note we only check for duplicates when we know we just inserted the object name in to the map, as this
                // ensure's that we only insert each duplicate once
                if ( classes ) {
                    auto duplicateClassIt = duplicateSharedCacheClassMap.find(objectLocation.name);
                    if ( duplicateClassIt != duplicateSharedCacheClassMap.end() ) {
                        seenObjectsMap.insert({ objectLocation.name, duplicateClassIt->second });
                    }
                }
            }

            PrebuiltLoader::BindTarget valueTarget = { image.jitLoader, objectLocation.valueRuntimeOffset };
            seenObjectsMap.insert({ objectLocation.name, valueTarget });
        }
    }

    __block uint32_t numClassesWithDuplicates = 0;
    __block uint32_t totalDuplicates          = 0;
    seenObjectsMap.forEachEntry(^(const char* const& key, const PrebuiltLoader::BindTarget** values,
                                  uint64_t valuesCount) {
        if ( valuesCount != 1 ) {
            ++numClassesWithDuplicates;
            totalDuplicates += valuesCount;
        }
    });

    // If we have closure class names, we need to make a hash table for them.
    if ( !objectNames.empty() ) {
        objc::PerfectHash phash;
        objc::PerfectHash::make_perfect(objectNames, phash);
        size_t size = ObjCClassOpt::size(phash, numClassesWithDuplicates, totalDuplicates);
        hashTable.resize(size);
        //printf("Class table size: %lld\n", size);
        ObjCClassOpt* resultHashTable = (ObjCClassOpt*)hashTable.begin();
        resultHashTable->write(phash, objectNameMap.array(), seenObjectsMap,
                               numClassesWithDuplicates, totalDuplicates);
    }
}

//////////////////////// PrebuiltObjC /////////////////////////////////

PrebuiltObjC::~PrebuiltObjC()
{
    for ( ObjCOptimizerImage& objcImage : objcImages ) {
        objcImage.~ObjCOptimizerImage();
    }
}

void PrebuiltObjC::commitImage(const ObjCOptimizerImage& image)
{
    // As this image is still valid, then add its intermediate results to the main tables
    for ( const auto& stringAndDuplicate : image.duplicateSharedCacheClassMap ) {
        // Note we want to overwrite any existing entries here.  We want the last seen
        // class with a duplicate to be in the map as writeClassOrProtocolHashTable walks the images
        // from back to front.
        duplicateSharedCacheClassMap[stringAndDuplicate.first] = stringAndDuplicate.second;
    }

    // Selector results
    // Note we don't need to add the selector binds here.  Its easier just to process them later from each image
    for ( const auto& stringAndTarget : image.selectorMap ) {
        closureSelectorMap[stringAndTarget.first] = stringAndTarget.second;
        closureSelectorStrings.push_back(stringAndTarget.first);
    }
}

void PrebuiltObjC::generateHashTables(RuntimeState& state)
{
    // Write out the class table
    writeClassOrProtocolHashTable(state, true, objcImages, classesHashTable, duplicateSharedCacheClassMap);

    // Write out the protocol table
    writeClassOrProtocolHashTable(state, false, objcImages, protocolsHashTable, duplicateSharedCacheClassMap);

    // If we have closure selectors, we need to make a hash table for them.
    if ( !closureSelectorStrings.empty() ) {
        objc::PerfectHash phash;
        objc::PerfectHash::make_perfect(closureSelectorStrings, phash);
        size_t size = ObjCStringTable::size(phash);
        selectorsHashTable.resize(size);
        //printf("Selector table size: %lld\n", size);
        selectorStringTable = (ObjCStringTable*)selectorsHashTable.begin();
        selectorStringTable->write(phash, closureSelectorMap.array());
    }
}

void PrebuiltObjC::generatePerImageFixups(RuntimeState& state, uint32_t pointerSize)
{
    // Find the largest JIT loader index so that we know how many images we might serialize
    uint16_t largestLoaderIndex = 0;
    for ( const Loader* l : state.loaded ) {
        if ( !l->isPrebuilt ) {
            JustInTimeLoader* jl = (JustInTimeLoader*)l;
            assert(jl->ref.app);
            largestLoaderIndex = std::max(largestLoaderIndex, jl->ref.index);
        }
    }
    ++largestLoaderIndex;

    imageFixups.reserve(largestLoaderIndex);
    for ( uint16_t i = 0; i != largestLoaderIndex; ++i ) {
        imageFixups.default_constuct_back();
    }

    // Add per-image fixups
    for ( ObjCOptimizerImage& image : objcImages ) {
        if ( image.diag.hasError() )
            continue;

        ObjCImageFixups& fixups = imageFixups[image.jitLoader->ref.index];

        // Copy all the binary info for use later when applying fixups
        fixups.binaryInfo = image.binaryInfo;

        // Protocol ISA references
        // These are a single boolean value for each protocol to identify if it is canonical or not
        // We convert from bool to uint8_t as that seems better for saving to disk.
        if ( !image.protocolISAFixups.empty() ) {
            fixups.protocolISAFixups.reserve(image.protocolISAFixups.count());
            for ( bool isCanonical : image.protocolISAFixups )
                fixups.protocolISAFixups.push_back(isCanonical ? 1 : 0);
        }

        // Selector references.
        // These are a BindTargetRef for every selector reference to fixup
        if ( !image.selectorFixups.empty() ) {
            fixups.selectorReferenceFixups.reserve(image.selectorFixups.count());
            for ( const PrebuiltLoader::BindTargetRef& target : image.selectorFixups ) {
                fixups.selectorReferenceFixups.push_back(target);
            }
        }
    }
}

// Visits each selector reference once, in order.  Note the order this visits selector references has to
// match for serializing/deserializing the PrebuiltLoader.
void PrebuiltObjC::forEachSelectorReferenceToUnique(RuntimeState&                                state,
                                                    const dyld3::MachOAnalyzer*                  ma,
                                                    uint64_t                                     loadAddress,
                                                    const ObjCBinaryInfo&                        binaryInfo,
                                                    const dyld3::MachOAnalyzer::VMAddrConverter& vmAddrConverter,
                                                    void (^callback)(uint64_t selectorReferenceRuntimeOffset, uint64_t selectorStringRuntimeOffset))

{
    uint32_t pointerSize = ma->pointerSize();
    if ( binaryInfo.selRefsCount != 0 ) {
        ma->forEachObjCSelectorReference(binaryInfo.selRefsRuntimeOffset, binaryInfo.selRefsCount, vmAddrConverter,
                                         ^(uint64_t selRefVMAddr, uint64_t selRefTargetVMAddr, bool& stop) {
                                             uint64_t selectorReferenceRuntimeOffset = selRefVMAddr - loadAddress;
                                             uint64_t selectorStringRuntimeOffset    = selRefTargetVMAddr - loadAddress;
                                             callback(selectorReferenceRuntimeOffset, selectorStringRuntimeOffset);
                                         });
    }

    // We only make the callback for method list selrefs which are not already covered by the __objc_selrefs section.
    // For pointer based method lists, this is all sel ref pointers.
    // For relative method lists, we should always point to the __objc_selrefs section.  This was checked earlier, so
    // we skip this callback on relative method lists as we know here they must point to the (already uniqied) __objc_selrefs.
    auto visitMethod = ^(uint64_t methodVMAddr, const dyld3::MachOAnalyzer::ObjCMethod& method, bool& stop) {
        uint64_t selectorReferenceRuntimeOffset = method.nameLocationVMAddr - loadAddress;
        uint64_t selectorStringRuntimeOffset    = method.nameVMAddr - loadAddress;
        callback(selectorReferenceRuntimeOffset, selectorStringRuntimeOffset);
    };

    auto visitMethodList = ^(uint64_t methodListVMAddr) {
        if ( methodListVMAddr == 0 )
            return;
        uint64_t methodListRuntimeOffset = methodListVMAddr - loadAddress;
        if ( ma->objcMethodListIsRelative(methodListRuntimeOffset) )
            return;
        ma->forEachObjCMethod(methodListVMAddr, vmAddrConverter, 0, visitMethod);
    };

    if ( binaryInfo.hasClassMethodListsToUnique && (binaryInfo.classListCount != 0) ) {
        auto visitClass = ^(uint64_t classVMAddr, uint64_t classSuperclassVMAddr, uint64_t classDataVMAddr,
                            const dyld3::MachOAnalyzer::ObjCClassInfo& objcClass, bool isMetaClass, bool& stop) {
            visitMethodList(objcClass.baseMethodsVMAddr(pointerSize));
        };
        ma->forEachObjCClass(binaryInfo.classListRuntimeOffset, binaryInfo.classListCount, vmAddrConverter, visitClass);
    }

    if ( binaryInfo.hasCategoryMethodListsToUnique && (binaryInfo.categoryCount != 0) ) {
        auto visitCategory = ^(uint64_t categoryVMAddr, const dyld3::MachOAnalyzer::ObjCCategory& objcCategory, bool& stop) {
            visitMethodList(objcCategory.instanceMethodsVMAddr);
            visitMethodList(objcCategory.classMethodsVMAddr);
        };
        ma->forEachObjCCategory(binaryInfo.categoryListRuntimeOffset, binaryInfo.categoryCount, vmAddrConverter, visitCategory);
    }

    if ( binaryInfo.hasProtocolMethodListsToUnique && (binaryInfo.protocolListCount != 0) ) {
        auto visitProtocol = ^(uint64_t protocolVMAddr, const dyld3::MachOAnalyzer::ObjCProtocol& objCProtocol, bool& stop) {
            visitMethodList(objCProtocol.instanceMethodsVMAddr);
            visitMethodList(objCProtocol.classMethodsVMAddr);
            visitMethodList(objCProtocol.optionalInstanceMethodsVMAddr);
            visitMethodList(objCProtocol.optionalClassMethodsVMAddr);
        };
        ma->forEachObjCProtocol(binaryInfo.protocolListRuntimeOffset, binaryInfo.protocolListCount, vmAddrConverter, visitProtocol);
    }
}

void PrebuiltObjC::make(Diagnostics& diag, RuntimeState& state)
{
    const DyldSharedCache* dyldCache = state.config.dyldCache.addr;
    if ( dyldCache == nullptr )
        return;

    STACK_ALLOC_ARRAY(const Loader*, jitLoaders, state.loaded.size());
    for (const Loader* ldr : state.loaded)
        jitLoaders.push_back(ldr);

    // If we have the read only data, make sure it has a valid selector table inside.
    const objc::ClassHashTable*    objcClassOpt    = nullptr;
    const objc::SelectorHashTable* objcSelOpt      = nullptr;
    const objc::ProtocolHashTable* objcProtocolOpt = nullptr;
    const void*                    headerInfoRO    = nullptr;
    const void*                    headerInfoRW    = nullptr;
    if ( const objc_opt::objc_opt_t* optObjCHeader = dyldCache->objcOpt() ) {
        objcClassOpt    = optObjCHeader->classOpt();
        objcSelOpt      = optObjCHeader->selectorOpt();
        objcProtocolOpt = optObjCHeader->protocolOpt();
        headerInfoRO    = optObjCHeader->headeropt_ro();
        headerInfoRW    = optObjCHeader->headeropt_rw();
    }

    if ( !objcClassOpt || !objcSelOpt || !objcProtocolOpt )
        return;

    // Make sure we have the pointers section with the pointer to the protocol class
    const void* objcOptPtrs = dyldCache->objcOptPtrs();
    if ( objcOptPtrs == nullptr )
        return;

    uint32_t pointerSize = state.mainExecutableLoader->loadAddress(state)->pointerSize();

    {
        uint64_t classProtocolVMAddr = (pointerSize == 8) ? *(uint64_t*)objcOptPtrs : *(uint32_t*)objcOptPtrs;
#if BUILDING_DYLD
        // As we are running in dyld, the cache is live
    #if __has_feature(ptrauth_calls)
        // If we are on arm64e, the protocol ISA in the shared cache was signed.  We don't
        // want the signature bits in the encoded value
        classProtocolVMAddr = (uint64_t)__builtin_ptrauth_strip((void*)classProtocolVMAddr, ptrauth_key_asda);
    #endif
        objcProtocolClassCacheOffset = classProtocolVMAddr - (uint64_t)dyldCache;
#elif BUILDING_CLOSURE_UTIL
        // FIXME: This assumes an on-disk cache
        classProtocolVMAddr          = dyldCache->makeVMAddrConverter(false).convertToVMAddr(classProtocolVMAddr);
        objcProtocolClassCacheOffset = classProtocolVMAddr - dyldCache->unslidLoadAddress();
#else
        // Running offline so the cache is not live
        objcProtocolClassCacheOffset = classProtocolVMAddr - dyldCache->unslidLoadAddress();
#endif // BUILDING_DYLD
    }

    // Find all the images with valid objc info
    SharedCacheImagesMapTy sharedCacheImagesMap;
    for ( const Loader* ldr : jitLoaders ) {
        const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)ldr->loadAddress(state);

        const MachOAnalyzer::ObjCImageInfo* objcImageInfo = ma->objcImageInfo();
        if ( objcImageInfo == nullptr )
            continue;

        if ( ldr->dylibInDyldCache ) {
            // Add shared cache images to a map so that we can see them later for looking up classes
            std::optional<uint16_t> objcIndex = objc::getPreoptimizedHeaderRWIndex(headerInfoRO, headerInfoRW, ma);
            if ( !objcIndex.has_value() )
                return;
            sharedCacheImagesMap.insert({ *objcIndex, { ma, ldr } });
            continue;
        }

        // If we have a root of libobjc, just give up for now
        if ( ldr->matchesPath("/usr/lib/libobjc.A.dylib") )
            return;

        // dyld can see the strings in Fairplay binaries and protected segments, but other tools cannot.
        // Skip generating the PrebuiltObjC in these other cases
#if !BUILDING_DYLD
        // Find FairPlay encryption range if encrypted
        uint32_t fairPlayFileOffset;
        uint32_t fairPlaySize;
        if ( ma->isFairPlayEncrypted(fairPlayFileOffset, fairPlaySize) )
            return;

        __block bool hasProtectedSegment = false;
        ma->forEachSegment(^(const dyld3::MachOAnalyzer::SegmentInfo& segInfo, bool& stop) {
            if ( segInfo.isProtected ) {
                hasProtectedSegment = true;
                stop                = true;
            }
        });
        if ( hasProtectedSegment )
            return;
#endif

        // This image is good so record it for use later.
        objcImages.emplace_back((const JustInTimeLoader*)ldr, ma->preferredLoadAddress(), pointerSize);
        ObjCOptimizerImage& image = objcImages.back();
        image.jitLoader           = (const JustInTimeLoader*)ldr;

        // Set the offset to the objc image info
        image.binaryInfo.imageInfoRuntimeOffset = (uint64_t)objcImageInfo - (uint64_t)ma;

        // Get the range of a section which is required to contain pointers, i.e., be pointer sized.
        auto getPointerBasedSection = ^(const char* name, uint64_t& runtimeOffset, uint32_t& pointerCount) {
            uint64_t offset;
            uint64_t count;
            if ( ma->findObjCDataSection(name, offset, count) ) {
                if ( (count % pointerSize) != 0 ) {
                    image.diag.error("Invalid objc pointer section size");
                    return;
                }
                runtimeOffset = offset;
                pointerCount  = (uint32_t)count / pointerSize;
            }
            else {
                runtimeOffset = 0;
                pointerCount  = 0;
            }
        };

        // Find the offsets to all other sections we need for the later optimizations
        getPointerBasedSection("__objc_selrefs", image.binaryInfo.selRefsRuntimeOffset, image.binaryInfo.selRefsCount);
        getPointerBasedSection("__objc_classlist", image.binaryInfo.classListRuntimeOffset, image.binaryInfo.classListCount);
        getPointerBasedSection("__objc_catlist", image.binaryInfo.categoryListRuntimeOffset, image.binaryInfo.categoryCount);
        getPointerBasedSection("__objc_protolist", image.binaryInfo.protocolListRuntimeOffset, image.binaryInfo.protocolListCount);
    }

    for ( ObjCOptimizerImage& image : objcImages ) {
        if ( image.diag.hasError() )
            continue;

        optimizeObjCClasses(state, objcClassOpt, sharedCacheImagesMap, duplicateSharedCacheClassMap, image);
        if ( image.diag.hasError() )
            continue;

        optimizeObjCProtocols(state, objcProtocolOpt, sharedCacheImagesMap, image);
        if ( image.diag.hasError() )
            continue;

        optimizeObjCSelectors(state, objcSelOpt, closureSelectorMap, image);
        if ( image.diag.hasError() )
            continue;

        commitImage(image);
    }

    // If we successfully analyzed the classes and selectors, we can now emit their data
    generateHashTables(state);
    generatePerImageFixups(state, pointerSize);

    builtObjC = true;
}

uint32_t PrebuiltObjC::serializeFixups(const Loader& jitLoader, BumpAllocator& allocator) const
{
    if ( !builtObjC )
        return 0;

    assert(jitLoader.ref.app);
    uint16_t index = jitLoader.ref.index;

    const ObjCImageFixups& fixups = imageFixups[index];

    if ( fixups.binaryInfo.imageInfoRuntimeOffset == 0 ) {
        // No fixups to apply
        return 0;
    }

    uint32_t                         serializationStart = (uint32_t)allocator.size();
    BumpAllocatorPtr<ObjCBinaryInfo> fixupInfo(allocator, serializationStart);

    allocator.append(&fixups.binaryInfo, sizeof(fixups.binaryInfo));

    // Protocols
    if ( !fixups.protocolISAFixups.empty() ) {
        // If we have protocol fixups, then we must have 1 for every protocol in this image.
        assert(fixups.protocolISAFixups.count() == fixups.binaryInfo.protocolListCount);

        uint16_t protocolArrayOff       = allocator.size() - serializationStart;
        fixupInfo->protocolFixupsOffset = protocolArrayOff;
        allocator.zeroFill(fixups.protocolISAFixups.count() * sizeof(uint8_t));
        allocator.align(8);
        BumpAllocatorPtr<uint8_t> protocolArray(allocator, serializationStart + protocolArrayOff);
        memcpy(protocolArray.get(), fixups.protocolISAFixups.begin(), fixups.protocolISAFixups.count() * sizeof(uint8_t));
    }

    // Selector references
    if ( !fixups.selectorReferenceFixups.empty() ) {
        uint16_t selectorsArrayOff                = allocator.size() - serializationStart;
        fixupInfo->selectorReferencesFixupsOffset = selectorsArrayOff;
        fixupInfo->selectorReferencesFixupsCount  = (uint32_t)fixups.selectorReferenceFixups.count();
        allocator.zeroFill(fixups.selectorReferenceFixups.count() * sizeof(PrebuiltLoader::BindTargetRef));
        BumpAllocatorPtr<uint8_t> selectorsArray(allocator, serializationStart + selectorsArrayOff);
        memcpy(selectorsArray.get(), fixups.selectorReferenceFixups.begin(), fixups.selectorReferenceFixups.count() * sizeof(PrebuiltLoader::BindTargetRef));
    }

    return serializationStart;
}

} // namespace dyld4


// Temporary copy of the old hash tables, to let the split cache branch load old hash tables
namespace legacy_objc_opt
{

uint32_t objc_stringhash_t::hash(const char *key, size_t keylen) const
{
    uint64_t val = objc::lookup8((uint8_t*)key, keylen, salt);
    uint32_t index = (uint32_t)(val>>shift) ^ scramble[tab[val&mask]];
    return index;
}


const header_info_rw *getPreoptimizedHeaderRW(const struct header_info *const hdr,
                                              void* headerInfoRO, void* headerInfoRW)
{
    const objc_headeropt_ro_t* hinfoRO = (const objc_headeropt_ro_t*)headerInfoRO;
    const objc_headeropt_rw_t* hinfoRW = (const objc_headeropt_rw_t*)headerInfoRW;
    int32_t index = hinfoRO->index(hdr);
    assert(hinfoRW->entsize == sizeof(header_info_rw));
    return &hinfoRW->headers[index];
}

}
