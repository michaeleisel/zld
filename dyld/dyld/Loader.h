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

#ifndef Loader_h
#define Loader_h

#include <ptrauth.h>
#include <stdint.h>

#include "MachOLoaded.h"
#include "Array.h"
#include "DyldDelegates.h"
#include "DyldProcessConfig.h"

#if BUILDING_CACHE_BUILDER
  #include <string>
  #include <unordered_map>
#endif

class DyldSharedCache;

using dyld3::MachOLoaded;
using dyld3::MachOAnalyzer;
using dyld3::Array;
typedef dyld3::MachOLoaded::PointerMetaData    PointerMetaData;

namespace dyld4 {

class PrebuiltLoader;
class JustInTimeLoader;
class RuntimeState;
class DyldCacheDataConstLazyScopedWriter;

//
//  At runtime there is one Loader object for each mach-o image loaded.
//  Loader is an abstract base class.  The two concrete classes
//  instantiated at runtime are PrebuiltLoader and JustInTimeLoader.
//  PrebuiltLoader objects mmap()ed in read-only from disk.
//  JustInTimeLoader objects are malloc()ed.
//
class Loader
{
public:
    struct LoaderRef {
                                LoaderRef(bool appPrebuilt, uint16_t indexInSet) : index(indexInSet), app(appPrebuilt) {}
        const PrebuiltLoader*   loader(const RuntimeState& state) const;

        uint16_t    index       : 15,   // index into PrebuiltLoaderSet
                    app         :  1;   // app vs dyld cache PrebuiltLoaderSet

        bool                    isMissingWeakImage() const { return ((index == 0x7fff) && (app == 0)); }
        static const LoaderRef  missingWeakImage() { return LoaderRef(0, 0x7fff); }
    };

    const uint32_t      magic;                    // kMagic
    const uint16_t      isPrebuilt         :  1,  // PrebuiltLoader vs JustInTimeLoader
                        dylibInDyldCache   :  1,
                        hasObjC            :  1,
                        mayHavePlusLoad    :  1,
                        hasReadOnlyData    :  1,  // __DATA_CONST
                        neverUnload        :  1,  // part of launch or has non-unloadable data (e.g. objc, tlv)
                        leaveMapped        :  1,  // RTLD_NODELETE
                        padding            :  8;
    LoaderRef           ref;

    enum ExportedSymbolMode { staticLink, shallow, dlsymNext, dlsymSelf };
    
    struct LoadChain
    {
        const LoadChain*   previous;
        const Loader*      image;
    };

    struct LoadOptions
    {
        typedef const Loader* (^Finder)(Diagnostics& diag, dyld3::Platform, const char* loadPath, const LoadOptions& options);
        typedef void          (^Missing)(const char* pathNotFound);

        bool        launching           = false;
        bool        staticLinkage       = false;
        bool        canBeMissing        = false;
        bool        rtldLocal           = false;
        bool        rtldNoDelete        = false;
        bool        rtldNoLoad          = false;
        bool        insertedDylib       = false;
        bool        canBeDylib          = false;
        bool        canBeBundle         = false;
        bool        canBeExecutable     = false;
        bool        forceUnloadable     = false;
        bool        useFallBackPaths    = true;
        LoadChain*  rpathStack          = nullptr;
        Finder      finder              = nullptr;
        Missing     pathNotFoundHandler = nullptr;
    };

    struct ResolvedSymbol {
        enum class Kind { rebase, bindToImage, bindAbsolute };
        const Loader*   targetLoader;
        const char*     targetSymbolName;
        uint64_t        targetRuntimeOffset;
        Kind            kind;
        bool            isCode;
        bool            isWeakDef;
    };
    struct BindTarget { const Loader* loader; uint64_t runtimeOffset; };
    enum class DependentKind : uint8_t { normal=0, weakLink=1, reexport=2, upward=3 };

    // stored in PrebuiltLoader when it references a file on disk
    struct FileValidationInfo
    {
        uint64_t    sliceOffset;
        uint64_t    inode;
        uint64_t    mtime;
        uint8_t     cdHash[20];         // to validate file has not changed since PrebuiltLoader was built
        bool        checkInodeMtime;
        bool        checkCdHash;
    };

    // stored in PrebuiltLoaders and generated on the fly by JustInTimeLoaders, passed to mapSegments()
    struct Region
    {
        uint64_t    vmOffset     : 59,
                    perms        :  3,
                    isZeroFill   :  1,
                    readOnlyData :  1;
        uint32_t    fileOffset;
        uint32_t    fileSize;       // mach-o files are limited to 4GB, but zero fill data can be very large
    };

    // Records which binds are to flat-namespace, lazy symbols
    struct MissingFlatLazySymbol
    {
        const char* symbolName;
        uint32_t    bindTargetIndex;
    };

    struct DylibPatch {
        int64_t             overrideOffsetOfImpl;   // this is a signed so that it can reach re-expoted symbols in another dylib
    };

    // these are the "virtual" methods that JustInTimeLoader and PrebuiltLoader implement
    const MachOLoaded*   loadAddress(RuntimeState& state) const;
    const char*          path() const;
    bool                 contains(RuntimeState& state, const void* addr, const void** segAddr, uint64_t* segSize, uint8_t* segPerms) const;
    bool                 matchesPath(const char* path) const;
    FileID               fileID() const;
    uint32_t             dependentCount() const;
    Loader*              dependent(const RuntimeState& state, uint32_t depIndex, DependentKind* kind=nullptr) const;
    bool                 hiddenFromFlat(bool forceGlobal=false) const;
    bool                 representsCachedDylibIndex(uint16_t dylibIndex) const;
    bool                 getExportsTrie(uint64_t& runtimeOffset, uint32_t& size) const;
    void                 loadDependents(Diagnostics& diag, RuntimeState& state, const LoadOptions& options);
    void                 unmap(RuntimeState& state, bool force=false) const;
    void                 applyFixups(Diagnostics&, RuntimeState&, DyldCacheDataConstLazyScopedWriter&, bool allowLazyBinds) const;
    bool                 overridesDylibInCache(const DylibPatch*& patchTable, uint16_t& cacheDylibOverriddenIndex) const;

    // these are private
    bool                 hasBeenFixedUp(RuntimeState&) const;
    bool                 beginInitializers(RuntimeState&);
    void                 runInitializers(RuntimeState&) const;

    typedef void (^FixUpHandler)(uint64_t fixupLocRuntimeOffset, uint64_t addend, PointerMetaData pmd, const ResolvedSymbol& target, bool& stop);
    typedef void (^CacheWeakDefOverride)(uint32_t cachedDylibIndex, uint32_t cachedDylibVMOffset, const ResolvedSymbol& target);

    // helper functions
    const char*             leafName() const;
    const MachOAnalyzer*    analyzer(RuntimeState& state) const { return (MachOAnalyzer*)loadAddress(state); }
    bool                    hasExportedSymbol(Diagnostics& diag, RuntimeState&, const char* symbolName, ExportedSymbolMode mode, ResolvedSymbol* result, dyld3::Array<const Loader*>* searched=nullptr) const;
    void                    logSegmentsFromSharedCache(RuntimeState& state) const;
    void                    makeSegmentsReadOnly(RuntimeState& state) const;
    ResolvedSymbol          resolveSymbol(Diagnostics& diag, RuntimeState&, int libOrdinal, const char* symbolName, bool weakImport,
                                          bool lazyBind, CacheWeakDefOverride patcher, bool buildingCache=false) const;
    void                    runInitializersBottomUp(RuntimeState&, Array<const Loader*>& danglingUpwards) const;
    void                    runInitializersBottomUpPlusUpwardLinks(RuntimeState&) const;
    void                    findAndRunAllInitializers(RuntimeState&) const;
    bool                    hasMagic() const;
    void                    applyCachePatchesToOverride(RuntimeState& state, const Loader* dylibToPatch,
                                                        uint16_t overriddenDylibIndex, const DylibPatch* patches,
                                                        DyldCacheDataConstLazyScopedWriter& cacheDataConst) const;
    void                    applyCachePatchesTo(RuntimeState& state, const Loader* dylibToPatch, DyldCacheDataConstLazyScopedWriter& cacheDataConst) const;
    void                    applyFixupsGeneric(Diagnostics&, RuntimeState& state, const Array<const void*>& bindTargets,
                                               const Array<const void*>& overrideBindTargets, bool laziesMustBind,
                                               const Array<MissingFlatLazySymbol>& missingFlatLazySymbols) const;
    const JustInTimeLoader* isJustInTimeLoader() const { return (this->isPrebuilt ? nullptr               : (JustInTimeLoader*)this); };
    const PrebuiltLoader*   isPrebuiltLoader() const   { return (this->isPrebuilt ? (PrebuiltLoader*)this : nullptr); };


    static uintptr_t        resolvedAddress(RuntimeState& state, const ResolvedSymbol& symbol);

    static void             appendHexNibble(uint8_t value, char*& p);
    static void             appendHexByte(uint8_t value, char*& p);
    static void             logLoad(RuntimeState&, const MachOLoaded* ml, const char* path);
    static void             uuidToStr(uuid_t uuid, char  uuidStr[64]);
    static void             applyInterposingToDyldCache(RuntimeState& state);
    static uintptr_t        interpose(RuntimeState& state, uintptr_t value, const Loader* forLoader=nullptr);
    static const Loader*    alreadyLoaded(RuntimeState& state, const char* loadPath);
    static const Loader*    getLoader(Diagnostics& diag, RuntimeState& state, const char* loadPath, const LoadOptions& options);
    static const char*      leafName(const char* path);
    static void             forEachResolvedAtPathVar(RuntimeState& state, const char* loadPath, const LoadOptions& options, ProcessConfig::PathOverrides::Type type, bool& stop,
                                                     void (^handler)(const char* possiblePath, ProcessConfig::PathOverrides::Type type, bool& stop));
    static void             forEachPath(Diagnostics& diag, RuntimeState& state, const char* requestedPath, const LoadOptions& options,
                                        void (^handler)(const char* possiblePath, ProcessConfig::PathOverrides::Type, bool& stop));

    static void             addWeakDefsToMap(RuntimeState& state, const dyld3::Array<const Loader*>& newLoaders);


protected:

    enum { kMagic = 'l4yd' };

    static const uint16_t kNoUnzipperedTwin = 0xFFFF;

    struct InitialOptions
    {
              InitialOptions();
              InitialOptions(const Loader& other);
        bool inDyldCache        = false;
        bool hasObjc            = false;
        bool mayHavePlusLoad    = false;
        bool roData             = false;
        bool neverUnloaded      = false;
        bool leaveMapped        = false;
   };

    struct CodeSignatureInFile
    {
        uint32_t   fileOffset;
        uint32_t   size;
    };


                               Loader(const InitialOptions& options, bool prebuilt=false, bool prebuiltApp=false, bool prebuiltIndex=0)
                                       : magic(kMagic), isPrebuilt(prebuilt), dylibInDyldCache(options.inDyldCache),
                                         hasObjC(options.hasObjc), mayHavePlusLoad(options.mayHavePlusLoad), hasReadOnlyData(options.roData),
                                         neverUnload(options.neverUnloaded), leaveMapped(options.leaveMapped), padding(0),
                                         ref(prebuiltApp, prebuiltIndex) { }


    static bool                 expandAtLoaderPath(RuntimeState& state, const char* loadPath, const LoadOptions& options, const Loader* ldr, bool fromLCRPATH, char fixedPath[]);
    static bool                 expandAtExecutablePath(RuntimeState& state, const char* loadPath, const LoadOptions& options, bool fromLCRPATH, char fixedPath[]);
    static const Loader*        makeDiskLoader(Diagnostics& diag, RuntimeState& state, const char* path, const LoadOptions& options, bool overridesDyldCache, uint32_t dylibIndex);
    static const Loader*        makeDyldCacheLoader(Diagnostics& diag, RuntimeState& state, const char* path, const LoadOptions& options, uint32_t dylibIndex);

    static const MachOAnalyzer* mapSegments(Diagnostics&, RuntimeState&, const char* path, uint64_t vmSpace,
                                            const CodeSignatureInFile& codeSignature, bool hasCodeSignature,
                                            const Array<Region>& segments, bool neverUnloads, bool prebuilt, const FileValidationInfo&);

    static uint64_t             validateFile(Diagnostics& diag, const RuntimeState& state, int fd, const char* path,
                                             const Loader::CodeSignatureInFile& codeSignature, const Loader::FileValidationInfo& fileValidation);

    static uint16_t             indexOfUnzipperedTwin(const RuntimeState& state, uint16_t overrideIndex);

};

#if __has_feature(ptrauth_calls)
#define __ptrauth_dyld_address_auth __ptrauth(ptrauth_key_process_dependent_data, 1, 0)
#else
#define __ptrauth_dyld_address_auth
#endif

// On arm64e, signs the given pointer with the address of where it is stored.
// Other archs just have a regular pointer
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wptrauth-null-pointers"
template<typename T>
struct AuthenticatedValue
{
};

// Partial specialization for pointer types
template<typename T>
struct AuthenticatedValue<T*>
{
    AuthenticatedValue() {
        this->value = ptrauth_sign_unauthenticated(nullptr, ptrauth_key_process_dependent_data, this);
    }
    ~AuthenticatedValue() = default;
    AuthenticatedValue(const AuthenticatedValue& other) {
        this->value = ptrauth_auth_and_resign(other.value,
                                              ptrauth_key_process_dependent_data, &other,
                                              ptrauth_key_process_dependent_data, this);
    }
    AuthenticatedValue(AuthenticatedValue&& other) {
        this->value = ptrauth_auth_and_resign(other.value,
                                              ptrauth_key_process_dependent_data, &other,
                                              ptrauth_key_process_dependent_data, this);
        other.value = ptrauth_sign_unauthenticated(nullptr, ptrauth_key_process_dependent_data, &other);
    }
    AuthenticatedValue& operator=(const AuthenticatedValue& other) {
        this->value = ptrauth_auth_and_resign(other.value,
                                              ptrauth_key_process_dependent_data, &other,
                                              ptrauth_key_process_dependent_data, this);
        return *this;
    }
    AuthenticatedValue& operator=(AuthenticatedValue&& other) {
        this->value = ptrauth_auth_and_resign(other.value,
                                              ptrauth_key_process_dependent_data, &other,
                                              ptrauth_key_process_dependent_data, this);
        other.value = ptrauth_sign_unauthenticated(nullptr, ptrauth_key_process_dependent_data, &other);
        return *this;
    }

    // Add a few convenience methods for interoperating with values of the given type
    AuthenticatedValue(const T* other) {
        this->value = (void*)ptrauth_sign_unauthenticated(other, ptrauth_key_process_dependent_data, this);
    }
    AuthenticatedValue& operator=(const T* other) {
        this->value = (void*)ptrauth_sign_unauthenticated(other, ptrauth_key_process_dependent_data, this);
        return *this;
    }
    bool operator==(const T* other) const {
        return ptrauth_auth_data(this->value, ptrauth_key_process_dependent_data, this) == other;
    }
    bool operator!=(const T* other) const {
        return ptrauth_auth_data(this->value, ptrauth_key_process_dependent_data, this) != other;
    }

    T* operator->() {
        return (T*)ptrauth_auth_data(this->value, ptrauth_key_process_dependent_data, this);
    }

    const T* operator->() const {
        return (const T*)ptrauth_auth_data(this->value, ptrauth_key_process_dependent_data, this);
    }

    operator T*() {
        return (T*)ptrauth_auth_data(this->value, ptrauth_key_process_dependent_data, this);
    }

    operator T*() const {
        return (T*)ptrauth_auth_data(this->value, ptrauth_key_process_dependent_data, this);
    }

private:
    void* value;
};
#pragma clang diagnostic pop

#if __has_feature(ptrauth_calls)
    typedef AuthenticatedValue<Loader*> AuthLoader;
    typedef AuthenticatedValue<const Loader*> ConstAuthLoader;
#else
    typedef Loader* AuthLoader;
    typedef const Loader* ConstAuthLoader;
#endif

}

#endif /* Loader_h */
