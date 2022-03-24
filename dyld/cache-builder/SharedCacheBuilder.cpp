/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
 *
 * Copyright (c) 2014 Apple Inc. All rights reserved.
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
#include <dirent.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/mach_time.h>
#include <mach/shared_region.h>
#include <apfs/apfs_fsctl.h>
#include <iostream>

#include <CommonCrypto/CommonHMAC.h>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonDigestSPI.h>

#include "mach-o/dyld_priv.h"
#include "ClosureFileSystemNull.h"
#include "CodeSigningTypes.h"
#include "MachOFileAbstraction.hpp"
#include "SharedCacheBuilder.h"
#include "IMPCachesBuilder.hpp"
#include "JustInTimeLoader.h"
#include "DyldProcessConfig.h"
#include "DyldRuntimeState.h"

#include "FileUtils.h"
#include "JSONWriter.h"
#include "StringUtils.h"
#include "Trie.hpp"

using dyld4::JustInTimeLoader;
using dyld4::PrebuiltLoader;
using dyld4::Loader;
using dyld4::PrebuiltLoaderSet;
using dyld4::RuntimeState;
using dyld4::ProcessConfig;
using dyld4::KernelArgs;
using dyld4::SyscallDelegate;

#if __has_include("dyld_cache_config.h")
    #include "dyld_cache_config.h"
#else
    #define ARM_SHARED_REGION_START      0x1A000000ULL
    #define ARM_SHARED_REGION_SIZE       0x26000000ULL
    #define ARM64_SHARED_REGION_START   0x180000000ULL
    #define ARM64_SHARED_REGION_SIZE     0x100000000ULL
#endif

#if ARM64_SHARED_REGION_START == 0x7FFF00000000
    #define ARM64_DELTA_MASK 0x00FF000000000000
#else
    #define ARM64_DELTA_MASK 0x00FFFF0000000000
#endif

#ifndef ARM64_32_SHARED_REGION_START
    #define ARM64_32_SHARED_REGION_START 0x1A000000ULL
    #define ARM64_32_SHARED_REGION_SIZE  0x26000000ULL
#endif

#define  ARMV7K_CHAIN_BITS    0xC0000000

#define X86_64_32GB 0x800000000

// On x86_64, each subcache is made up of 3 1GB regions.  1 for each of RX, RW, RO
#define DISCONTIGUOUS_REGION_SIZE       0x40000000ULL
#define SUBCACHE_TEXT_LIMIT_X86_64      (DISCONTIGUOUS_REGION_SIZE / 2) // 512MB
#define SUBCACHE_TEXT_LIMIT_ARM64       0x20000000ULL // 512MB
#define SUBCACHE_TEXT_LIMIT_ARM64E      0x50000000ULL // 1.25GB
#define SUBCACHE_TEXT_LIMIT_ARM64_32    0x08000000ULL // 128MB
#define SUBCACHE_TEXT_LIMIT_ARMV7K      0x08000000ULL // 128MB

// The x86_64 simulator needs to back deploy on systems which only have 4GB of shared region size.  Use the old defines for that
#define SIM_DISCONTIGUOUS_RX   0x7FFF20000000ULL
#define SIM_DISCONTIGUOUS_RW   0x7FFF80000000ULL
#define SIM_DISCONTIGUOUS_RO   0x7FFFC0000000ULL
#define SIM_DISCONTIGUOUS_RX_SIZE (SIM_DISCONTIGUOUS_RW - SIM_DISCONTIGUOUS_RX)
#define SIM_DISCONTIGUOUS_RW_SIZE 0x40000000
#define SIM_DISCONTIGUOUS_RO_SIZE 0x3FE00000

const SharedCacheBuilder::ArchLayout SharedCacheBuilder::_s_archLayout[] = {
    { 0x7FF800000000ULL,            X86_64_32GB,                 SUBCACHE_TEXT_LIMIT_X86_64,    0x40000000, 0x00FFFF0000000000, "x86_64",       CS_PAGE_SIZE_4K,  14, 2, true,  true,  true, false  },
    { 0x7FF800000000ULL,            X86_64_32GB,                 SUBCACHE_TEXT_LIMIT_X86_64,    0x40000000, 0x00FFFF0000000000, "x86_64h",      CS_PAGE_SIZE_4K,  14, 2, true,  true,  true, false  },
    { SIM_DISCONTIGUOUS_RX,         0xEFE00000ULL,               0x0,                           0x40000000, 0x00FFFF0000000000, "sim-x86_64",   CS_PAGE_SIZE_4K,  14, 2, true,  true,  true, false  },
    { SIM_DISCONTIGUOUS_RX,         0xEFE00000ULL,               0x0,                           0x40000000, 0x00FFFF0000000000, "sim-x86_64h",  CS_PAGE_SIZE_4K,  14, 2, true,  true,  true, false  },
    { SHARED_REGION_BASE_I386,      SHARED_REGION_SIZE_I386,     0x0,                           0x00200000,                0x0, "i386",         CS_PAGE_SIZE_4K,  12, 0, false, false, true, false  },
    { ARM64_SHARED_REGION_START,    ARM64_SHARED_REGION_SIZE,    SUBCACHE_TEXT_LIMIT_ARM64,     0x02000000,   ARM64_DELTA_MASK, "arm64",        CS_PAGE_SIZE_4K,  14, 2, false, true,  false, true },
    { ARM64_SHARED_REGION_START,    ARM64_SHARED_REGION_SIZE,    0x0,                           0x02000000,   ARM64_DELTA_MASK, "sim-arm64",    CS_PAGE_SIZE_4K,  14, 2, false, true,  false, false },
#if SUPPORT_ARCH_arm64e
    { ARM64_SHARED_REGION_START,    ARM64_SHARED_REGION_SIZE,    SUBCACHE_TEXT_LIMIT_ARM64E,    0x02000000,   ARM64_DELTA_MASK, "arm64e",       CS_PAGE_SIZE_16K, 14, 2, false, true,  false, false },
#endif
#if SUPPORT_ARCH_arm64_32
    { ARM64_32_SHARED_REGION_START, ARM64_32_SHARED_REGION_SIZE, SUBCACHE_TEXT_LIMIT_ARM64_32,  0x02000000, 0xC0000000,         "arm64_32",     CS_PAGE_SIZE_16K, 14, 6, false, false, true, true  },
#endif
    { ARM_SHARED_REGION_START,      ARM_SHARED_REGION_SIZE,      0x0,                           0x02000000, 0xE0000000,         "armv7s",       CS_PAGE_SIZE_4K,  14, 4, false, false, true, false  },
    { ARM_SHARED_REGION_START,      ARM_SHARED_REGION_SIZE,      SUBCACHE_TEXT_LIMIT_ARMV7K,    0x00400000, ARMV7K_CHAIN_BITS,  "armv7k",       CS_PAGE_SIZE_4K,  14, 6, false, false, true, true  },
    { 0x40000000,                   0x40000000,                  0x0,                           0x02000000, 0x0,                "sim-x86",      CS_PAGE_SIZE_4K,  14, 0, false, false, true, false  }
};

// These are functions that are interposed by Instruments.app or ASan
const char* const SharedCacheBuilder::_s_neverStubEliminateSymbols[] = {
    "___bzero",
    "___cxa_atexit",
    "___cxa_throw",
    "__longjmp",
    "__objc_autoreleasePoolPop",
    "_accept",
    "_access",
    "_asctime",
    "_asctime_r",
    "_asprintf",
    "_atoi",
    "_atol",
    "_atoll",
    "_calloc",
    "_chmod",
    "_chown",
    "_close",
    "_confstr",
    "_ctime",
    "_ctime_r",
    "_dispatch_after",
    "_dispatch_after_f",
    "_dispatch_async",
    "_dispatch_async_f",
    "_dispatch_barrier_async_f",
    "_dispatch_group_async",
    "_dispatch_group_async_f",
    "_dispatch_source_set_cancel_handler",
    "_dispatch_source_set_event_handler",
    "_dispatch_sync_f",
    "_dlclose",
    "_dlopen",
    "_dup",
    "_dup2",
    "_endgrent",
    "_endpwent",
    "_ether_aton",
    "_ether_hostton",
    "_ether_line",
    "_ether_ntoa",
    "_ether_ntohost",
    "_fchmod",
    "_fchown",
    "_fclose",
    "_fdopen",
    "_fflush",
    "_fopen",
    "_fork",
    "_fprintf",
    "_free",
    "_freopen",
    "_frexp",
    "_frexpf",
    "_frexpl",
    "_fscanf",
    "_fstat",
    "_fstatfs",
    "_fstatfs64",
    "_fsync",
    "_ftime",
    "_getaddrinfo",
    "_getattrlist",
    "_getcwd",
    "_getgrent",
    "_getgrgid",
    "_getgrgid_r",
    "_getgrnam",
    "_getgrnam_r",
    "_getgroups",
    "_gethostbyaddr",
    "_gethostbyname",
    "_gethostbyname2",
    "_gethostent",
    "_getifaddrs",
    "_getitimer",
    "_getnameinfo",
    "_getpass",
    "_getpeername",
    "_getpwent",
    "_getpwnam",
    "_getpwnam_r",
    "_getpwuid",
    "_getpwuid_r",
    "_getsockname",
    "_getsockopt",
    "_gmtime",
    "_gmtime_r",
    "_if_indextoname",
    "_if_nametoindex",
    "_index",
    "_inet_aton",
    "_inet_ntop",
    "_inet_pton",
    "_initgroups",
    "_ioctl",
    "_lchown",
    "_lgamma",
    "_lgammaf",
    "_lgammal",
    "_link",
    "_listxattr",
    "_localtime",
    "_localtime_r",
    "_longjmp",
    "_lseek",
    "_lstat",
    "_malloc",
    "_malloc_create_zone",
    "_malloc_default_purgeable_zone",
    "_malloc_default_zone",
    "_malloc_destroy_zone",
    "_malloc_good_size",
    "_malloc_make_nonpurgeable",
    "_malloc_make_purgeable",
    "_malloc_set_zone_name",
    "_malloc_zone_from_ptr",
    "_mbsnrtowcs",
    "_mbsrtowcs",
    "_mbstowcs",
    "_memchr",
    "_memcmp",
    "_memcpy",
    "_memmove",
    "_memset",
    "_mktime",
    "_mlock",
    "_mlockall",
    "_modf",
    "_modff",
    "_modfl",
    "_munlock",
    "_munlockall",
    "_objc_autoreleasePoolPop",
    "_objc_setProperty",
    "_objc_setProperty_atomic",
    "_objc_setProperty_atomic_copy",
    "_objc_setProperty_nonatomic",
    "_objc_setProperty_nonatomic_copy",
    "_objc_storeStrong",
    "_open",
    "_opendir",
    "_poll",
    "_posix_memalign",
    "_pread",
    "_printf",
    "_pthread_attr_getdetachstate",
    "_pthread_attr_getguardsize",
    "_pthread_attr_getinheritsched",
    "_pthread_attr_getschedparam",
    "_pthread_attr_getschedpolicy",
    "_pthread_attr_getscope",
    "_pthread_attr_getstack",
    "_pthread_attr_getstacksize",
    "_pthread_condattr_getpshared",
    "_pthread_create",
    "_pthread_getschedparam",
    "_pthread_join",
    "_pthread_mutex_lock",
    "_pthread_mutex_unlock",
    "_pthread_mutexattr_getprioceiling",
    "_pthread_mutexattr_getprotocol",
    "_pthread_mutexattr_getpshared",
    "_pthread_mutexattr_gettype",
    "_pthread_rwlockattr_getpshared",
    "_pwrite",
    "_rand_r",
    "_read",
    "_readdir",
    "_readdir_r",
    "_readv",
    "_readv$UNIX2003",
    "_realloc",
    "_realpath",
    "_recv",
    "_recvfrom",
    "_recvmsg",
    "_remquo",
    "_remquof",
    "_remquol",
    "_scanf",
    "_send",
    "_sendmsg",
    "_sendto",
    "_setattrlist",
    "_setgrent",
    "_setitimer",
    "_setlocale",
    "_setpwent",
    "_shm_open",
    "_shm_unlink",
    "_sigaction",
    "_sigemptyset",
    "_sigfillset",
    "_siglongjmp",
    "_signal",
    "_sigpending",
    "_sigprocmask",
    "_sigwait",
    "_snprintf",
    "_sprintf",
    "_sscanf",
    "_stat",
    "_statfs",
    "_statfs64",
    "_strcasecmp",
    "_strcat",
    "_strchr",
    "_strcmp",
    "_strcpy",
    "_strdup",
    "_strerror",
    "_strerror_r",
    "_strlen",
    "_strncasecmp",
    "_strncat",
    "_strncmp",
    "_strncpy",
    "_strptime",
    "_strtoimax",
    "_strtol",
    "_strtoll",
    "_strtoumax",
    "_tempnam",
    "_time",
    "_times",
    "_tmpnam",
    "_tsearch",
    "_unlink",
    "_valloc",
    "_vasprintf",
    "_vfprintf",
    "_vfscanf",
    "_vprintf",
    "_vscanf",
    "_vsnprintf",
    "_vsprintf",
    "_vsscanf",
    "_wait",
    "_wait$UNIX2003",
    "_wait3",
    "_wait4",
    "_waitid",
    "_waitid$UNIX2003",
    "_waitpid",
    "_waitpid$UNIX2003",
    "_wcslen",
    "_wcsnrtombs",
    "_wcsrtombs",
    "_wcstombs",
    "_wordexp",
    "_write",
    "_writev",
    "_writev$UNIX2003",
    // <rdar://problem/22050956> always use stubs for C++ symbols that can be overridden
    "__ZdaPv",
    "__ZdlPv",
    "__Znam",
    "__Znwm",

    nullptr
};


inline uint32_t absolutetime_to_milliseconds(uint64_t abstime)
{
    return (uint32_t)(abstime/1000/1000);
}

// Handles building a list of input files to the SharedCacheBuilder itself.
class CacheInputBuilder {
public:
    CacheInputBuilder(const dyld3::closure::FileSystem& fileSystem,
                      const dyld3::GradedArchs& archs, dyld3::Platform reqPlatform)
    : fileSystem(fileSystem), reqArchs(archs), reqPlatform(reqPlatform) { }

    // Loads and maps any MachOs in the given list of files.
    void loadMachOs(std::vector<CacheBuilder::InputFile>& inputFiles,
                    std::vector<CacheBuilder::LoadedMachO>& dylibsToCache,
                    std::vector<CacheBuilder::LoadedMachO>& otherDylibs,
                    std::vector<CacheBuilder::LoadedMachO>& executables,
                    std::vector<CacheBuilder::LoadedMachO>& couldNotLoadFiles) {

        std::map<std::string, uint64_t> dylibInstallNameMap;
        for (CacheBuilder::InputFile& inputFile : inputFiles) {
            char realerPath[MAXPATHLEN];
            dyld3::closure::LoadedFileInfo loadedFileInfo = dyld3::MachOAnalyzer::load(inputFile.diag, fileSystem, inputFile.path, reqArchs, reqPlatform, realerPath);
            if ( (reqPlatform == dyld3::Platform::macOS) && inputFile.diag.hasError() ) {
                // Try again with iOSMac
                inputFile.diag.clearError();
                loadedFileInfo = dyld3::MachOAnalyzer::load(inputFile.diag, fileSystem, inputFile.path, reqArchs, dyld3::Platform::iOSMac, realerPath);
            }
            const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)loadedFileInfo.fileContent;
            if (ma == nullptr) {
                couldNotLoadFiles.emplace_back((CacheBuilder::LoadedMachO){ DyldSharedCache::MappedMachO(), loadedFileInfo, &inputFile });
                continue;
            }

            DyldSharedCache::MappedMachO mappedFile(inputFile.path, ma, loadedFileInfo.sliceLen, false, false,
                                                    loadedFileInfo.sliceOffset, loadedFileInfo.mtime, loadedFileInfo.inode);

            // The file can be loaded with the given slice, but we may still want to exlude it from the cache.
            if (ma->isDylib()) {
                std::string installName = ma->installName();

                const char* dylibPath = inputFile.path;
                if ( (installName != inputFile.path) && (reqPlatform == dyld3::Platform::macOS) ) {
                    // We now typically require that install names and paths match.  However symlinks may allow us to bring in a path which
                    // doesn't match its install name.
                    // For example:
                    //   /usr/lib/libstdc++.6.0.9.dylib is a real file with install name /usr/lib/libstdc++.6.dylib
                    //   /usr/lib/libstdc++.6.dylib is a symlink to /usr/lib/libstdc++.6.0.9.dylib
                    // So long as we add both paths (with one as an alias) then this will work, even if dylibs are removed from disk
                    // but the symlink remains.
                    char resolvedSymlinkPath[PATH_MAX];
                    if ( fileSystem.getRealPath(installName.c_str(), resolvedSymlinkPath) ) {
                        if (!strcmp(resolvedSymlinkPath, inputFile.path)) {
                            // Symlink is the install name and points to the on-disk dylib
                            //fprintf(stderr, "Symlink works: %s == %s\n", inputFile.path, installName.c_str());
                            dylibPath = installName.c_str();
                        }
                    }
                }

                if (!ma->canBePlacedInDyldCache(dylibPath, ^(const char* msg) {
                    inputFile.diag.warning("Dylib located at '%s' cannot be placed in cache because: %s", inputFile.path, msg);
                })) {
                    // keep list of all dylibs not placed in the dyld cache
                    otherDylibs.emplace_back((CacheBuilder::LoadedMachO){ mappedFile, loadedFileInfo, &inputFile });
                    continue;
                }

                // Otherwise see if we have another file with this install name
                auto iteratorAndInserted = dylibInstallNameMap.insert(std::make_pair(installName, dylibsToCache.size()));
                if (iteratorAndInserted.second) {
                    // We inserted the dylib so we haven't seen another with this name.
                    if (installName[0] != '@' && installName != inputFile.path) {
                        inputFile.diag.warning("Dylib located at '%s' has installname '%s'", inputFile.path, installName.c_str());
                    }

                    dylibsToCache.emplace_back((CacheBuilder::LoadedMachO){ mappedFile, loadedFileInfo, &inputFile });
                } else {
                    // We didn't insert this one so we've seen it before.
                    CacheBuilder::LoadedMachO& previousLoadedMachO = dylibsToCache[iteratorAndInserted.first->second];
                    inputFile.diag.warning("Multiple dylibs claim installname '%s' ('%s' and '%s')", installName.c_str(), inputFile.path, previousLoadedMachO.mappedFile.runtimePath.c_str());

                    // This is the "Good" one, overwrite
                    if (inputFile.path == installName) {
                        // Unload the old one
                        fileSystem.unloadFile(previousLoadedMachO.loadedFileInfo);

                        // And replace with this one.
                        previousLoadedMachO.mappedFile = mappedFile;
                        previousLoadedMachO.loadedFileInfo = loadedFileInfo;
                    }
                }
            } else if (ma->isBundle()) {

                if (!ma->canHavePrecomputedDlopenClosure(inputFile.path, ^(const char* msg) {
                    inputFile.diag.verbose("Dylib located at '%s' cannot prebuild dlopen closure in cache because: %s", inputFile.path, msg);
                }) ) {
                    fileSystem.unloadFile(loadedFileInfo);
                    continue;
                }
                otherDylibs.emplace_back((CacheBuilder::LoadedMachO){ mappedFile, loadedFileInfo, &inputFile });
            } else if (ma->isDynamicExecutable()) {

                // Let the platform exclude the file before we do anything else.
                if (platformExcludesExecutablePath(inputFile.path)) {
                    inputFile.diag.verbose("Platform excluded file\n");
                    fileSystem.unloadFile(loadedFileInfo);
                    continue;
                }
                executables.emplace_back((CacheBuilder::LoadedMachO){ mappedFile, loadedFileInfo, &inputFile });
            } else {
                inputFile.diag.verbose("Unsupported mach file type\n");
                fileSystem.unloadFile(loadedFileInfo);
            }
        }
    }

private:

    static bool platformExcludesExecutablePath_macOS(const std::string& path) {
        // We no longer support ROSP, so skip all paths which start with the special prefix
        if ( startsWith(path, "/System/Library/Templates/Data/") )
            return true;

        static const char* sAllowedPrefixes[] = {
            "/bin/",
            "/sbin/",
            "/usr/",
            "/System/",
            "/Library/Apple/System/",
            "/Library/Apple/usr/",
            "/System/Applications/Safari.app/",
            "/Library/CoreMediaIO/Plug-Ins/DAL/"                // temp until plugins moved or closured working
        };

        bool inSearchDir = false;
        for (const char* searchDir : sAllowedPrefixes ) {
            if ( strncmp(searchDir, path.c_str(), strlen(searchDir)) == 0 )  {
                inSearchDir = true;
                break;
            }
        }

        return !inSearchDir;
    }

    // Returns true if the current platform requires that this path be excluded from the shared cache
    // Note that this overrides any exclusion from anywhere else.
    bool platformExcludesExecutablePath(const std::string& path) {
        if ( (reqPlatform == dyld3::Platform::macOS) || (reqPlatform == dyld3::Platform::iOSMac) )
            return platformExcludesExecutablePath_macOS(path);
        return false;
    }

    const dyld3::closure::FileSystem&                   fileSystem;
    const dyld3::GradedArchs&                           reqArchs;
    dyld3::Platform                                     reqPlatform;
};

SharedCacheBuilder::SharedCacheBuilder(const DyldSharedCache::CreateOptions& options,
                                       const dyld3::closure::FileSystem& fileSystem)
    : CacheBuilder(options, fileSystem) {

    std::string targetArch = options.archs->name();
    if ( options.forSimulator && (options.archs == &dyld3::GradedArchs::x86_64) )
        targetArch = "sim-x86_64";
    else if ( options.forSimulator && (options.archs == &dyld3::GradedArchs::x86_64h) )
        targetArch = "sim-x86_64h";
    else if ( options.forSimulator && (options.archs == &dyld3::GradedArchs::arm64) )
        targetArch = "sim-arm64";

    for (const ArchLayout& layout : _s_archLayout) {
        if ( layout.archName == targetArch ) {
            _archLayout = &layout;
            _is64 = _archLayout->is64;
            break;
        }
    }

    if (!_archLayout) {
        _diagnostics.error("Tool was built without support for: '%s'", targetArch.c_str());
    }
}

static void verifySelfContained(const dyld3::closure::FileSystem& fileSystem,
                                std::vector<CacheBuilder::LoadedMachO>& dylibsToCache,
                                std::vector<CacheBuilder::LoadedMachO>& otherDylibs,
                                std::vector<CacheBuilder::LoadedMachO>& couldNotLoadFiles)
{
    // build map of dylibs
    __block std::map<std::string, const CacheBuilder::LoadedMachO*> knownDylibs;
    __block std::map<std::string, const CacheBuilder::LoadedMachO*> allDylibs;
    for (const CacheBuilder::LoadedMachO& dylib : dylibsToCache) {
        knownDylibs.insert({ dylib.mappedFile.runtimePath, &dylib });
        allDylibs.insert({ dylib.mappedFile.runtimePath, &dylib });
        if (const char* installName = dylib.mappedFile.mh->installName()) {
            knownDylibs.insert({ installName, &dylib });
            allDylibs.insert({ installName, &dylib });
        }
    }

    for (const CacheBuilder::LoadedMachO& dylib : otherDylibs) {
        allDylibs.insert({ dylib.mappedFile.runtimePath, &dylib });
        if (const char* installName = dylib.mappedFile.mh->installName())
            allDylibs.insert({ installName, &dylib });
    }

    for (const CacheBuilder::LoadedMachO& dylib : couldNotLoadFiles) {
        allDylibs.insert({ dylib.inputFile->path, &dylib });
    }

    // Exclude bad unzippered twins.  These are where a zippered binary links
    // an unzippered twin
    std::unordered_map<std::string, std::string> macOSPathToTwinPath;
    for (const CacheBuilder::LoadedMachO& dylib : dylibsToCache) {
        macOSPathToTwinPath[dylib.mappedFile.runtimePath] = "";
    }
    for (const CacheBuilder::LoadedMachO& dylib : dylibsToCache) {
        if ( startsWith(dylib.mappedFile.runtimePath, "/System/iOSSupport/") ) {
            std::string tail = dylib.mappedFile.runtimePath.substr(18);
            if ( macOSPathToTwinPath.find(tail) != macOSPathToTwinPath.end() )
                macOSPathToTwinPath[tail] = dylib.mappedFile.runtimePath;
        }
    }

    __block std::map<std::string, std::set<std::string>> badDylibs;
    for (const CacheBuilder::LoadedMachO& dylib : dylibsToCache) {
        if ( badDylibs.count(dylib.mappedFile.runtimePath) != 0 )
            continue;
        if ( dylib.mappedFile.mh->isZippered() ) {
            dylib.mappedFile.mh->forEachDependentDylib(^(const char* loadPath, bool isWeak, bool isReExport, bool isUpward, uint32_t compatVersion, uint32_t curVersion, bool& stop) {
                auto macOSAndTwinPath = macOSPathToTwinPath.find(loadPath);
                if ( macOSAndTwinPath != macOSPathToTwinPath.end() ) {
                    const std::string& twinPath = macOSAndTwinPath->second;
                    if ( badDylibs.count(twinPath) != 0 )
                        return;
                    knownDylibs.erase(twinPath);
                    badDylibs[twinPath].insert(std::string("evicting UIKitForMac binary as it is linked by zippered binary '") + dylib.mappedFile.runtimePath + "'");
                }
            });
        }
    }

    // HACK: Exclude some dylibs and transitive deps for now until we have project fixes
    __block std::set<std::string> badProjects;
    badProjects.insert("/System/Library/PrivateFrameworks/TuriCore.framework/Versions/A/TuriCore");
    badProjects.insert("/System/Library/PrivateFrameworks/UHASHelloExtensionPoint-macOS.framework/Versions/A/UHASHelloExtensionPoint-macOS");

    // check all dependencies to assure every dylib in cache only depends on other dylibs in cache
    __block bool doAgain = true;
    while ( doAgain ) {
        doAgain = false;
        // scan dylib list making sure all dependents are in dylib list
        for (const CacheBuilder::LoadedMachO& dylib : dylibsToCache) {
            if ( badDylibs.count(dylib.mappedFile.runtimePath) != 0 )
                continue;
            if ( badProjects.count(dylib.mappedFile.runtimePath) != 0 )
                continue;
            dylib.mappedFile.mh->forEachDependentDylib(^(const char* loadPath, bool isWeak, bool isReExport, bool isUpward, uint32_t compatVersion, uint32_t curVersion, bool& stop) {
                if (isWeak)
                    return;
                if ( badProjects.count(loadPath) != 0 ) {
                    // We depend on a bad dylib, so add this one to the list too
                    badProjects.insert(dylib.mappedFile.runtimePath);
                    badProjects.insert(dylib.mappedFile.mh->installName());
                    knownDylibs.erase(dylib.mappedFile.runtimePath);
                    knownDylibs.erase(dylib.mappedFile.mh->installName());
                    badDylibs[dylib.mappedFile.runtimePath].insert(std::string("Depends on bad project '") + loadPath + "'");
                    doAgain = true;
                    return;
                }
                char resolvedSymlinkPath[PATH_MAX];
                if ( knownDylibs.count(loadPath) == 0 ) {
                    // The loadPath was embedded when the dylib was built, but we may be in the process of moving
                    // a dylib with symlinks from old to new paths
                    // In this case, the realpath will tell us the new location
                    if ( fileSystem.getRealPath(loadPath, resolvedSymlinkPath) ) {
                        if ( strcmp(resolvedSymlinkPath, loadPath) != 0 ) {
                            loadPath = resolvedSymlinkPath;
                        }
                    }
                }
                if ( knownDylibs.count(loadPath) == 0 ) {
                    badDylibs[dylib.mappedFile.runtimePath].insert(std::string("Could not find dependency '") + loadPath + "'");
                    knownDylibs.erase(dylib.mappedFile.runtimePath);
                    knownDylibs.erase(dylib.mappedFile.mh->installName());
                    doAgain = true;
                }
            });
        }
    }

    // Now walk the dylibs which depend on missing dylibs and see if any of them are required binaries.
    for (auto badDylibsIterator : badDylibs) {
        const std::string& dylibRuntimePath = badDylibsIterator.first;
        auto requiredDylibIterator = allDylibs.find(dylibRuntimePath);
        if (requiredDylibIterator == allDylibs.end())
            continue;
        if (!requiredDylibIterator->second->inputFile->mustBeIncluded())
            continue;
        // This dylib is required so mark all dependencies as requried too
        __block std::vector<const CacheBuilder::LoadedMachO*> worklist;
        worklist.push_back(requiredDylibIterator->second);
        while (!worklist.empty()) {
            const CacheBuilder::LoadedMachO* dylib = worklist.back();
            worklist.pop_back();
            if (!dylib->mappedFile.mh)
                continue;
            dylib->mappedFile.mh->forEachDependentDylib(^(const char* loadPath, bool isWeak, bool isReExport, bool isUpward, uint32_t compatVersion, uint32_t curVersion, bool& stop) {
                if (isWeak)
                    return;
                auto dylibIterator = allDylibs.find(loadPath);
                if (dylibIterator != allDylibs.end()) {
                    if (dylibIterator->second->inputFile->state == CacheBuilder::InputFile::Unset) {
                        dylibIterator->second->inputFile->state = CacheBuilder::InputFile::MustBeIncludedForDependent;
                        worklist.push_back(dylibIterator->second);
                    }
                }
            });
        }
    }

    // FIXME: Make this an option we can pass in
    const bool evictLeafDylibs = true;
    if (evictLeafDylibs) {
        doAgain = true;
        while ( doAgain ) {
            doAgain = false;

            // build count of how many references there are to each dylib
            __block std::set<std::string> referencedDylibs;
            for (const CacheBuilder::LoadedMachO& dylib : dylibsToCache) {
                if ( badDylibs.count(dylib.mappedFile.runtimePath) != 0 )
                    continue;
                dylib.mappedFile.mh->forEachDependentDylib(^(const char* loadPath, bool isWeak, bool isReExport, bool isUpward, uint32_t compatVersion, uint32_t curVersion, bool &stop) {
                    referencedDylibs.insert(loadPath);
                });
            }

            // find all dylibs not referenced
            for (const CacheBuilder::LoadedMachO& dylib : dylibsToCache) {
                if ( badDylibs.count(dylib.mappedFile.runtimePath) != 0 )
                    continue;
                const char* installName = dylib.mappedFile.mh->installName();
                if ( (referencedDylibs.count(installName) == 0) && (dylib.inputFile->state == CacheBuilder::InputFile::MustBeExcludedIfUnused) ) {
                    badDylibs[dylib.mappedFile.runtimePath].insert(std::string("It has been explicitly excluded as it is unused"));
                    doAgain = true;
                }
            }
        }
    }

    // Move bad dylibs from dylibs to cache to other dylibs.
    for (const CacheBuilder::LoadedMachO& dylib : dylibsToCache) {
        auto i = badDylibs.find(dylib.mappedFile.runtimePath);
        if ( i != badDylibs.end()) {
            otherDylibs.push_back(dylib);
            for (const std::string& reason : i->second )
                otherDylibs.back().inputFile->diag.warning("Dylib located at '%s' not placed in shared cache because: %s", dylib.mappedFile.runtimePath.c_str(), reason.c_str());
        }
    }

    const auto& badDylibsLambdaRef = badDylibs;
    dylibsToCache.erase(std::remove_if(dylibsToCache.begin(), dylibsToCache.end(), [&](const CacheBuilder::LoadedMachO& dylib) {
        if (badDylibsLambdaRef.find(dylib.mappedFile.runtimePath) != badDylibsLambdaRef.end())
            return true;
        return false;
    }), dylibsToCache.end());
}

// This is the new build API which takes the raw files (which could be FAT) and tries to build a cache from them.
// We should remove the other build() method, or make it private so that this can wrap it.
void SharedCacheBuilder::build(std::vector<CacheBuilder::InputFile>& inputFiles,
                               std::vector<DyldSharedCache::FileAlias>& aliases) {
    // First filter down to files which are actually MachO's
    CacheInputBuilder cacheInputBuilder(_fileSystem, *_options.archs, _options.platform);

    std::vector<LoadedMachO> dylibsToCache;
    std::vector<LoadedMachO> otherDylibs;
    std::vector<LoadedMachO> executables;
    std::vector<LoadedMachO> couldNotLoadFiles;
    cacheInputBuilder.loadMachOs(inputFiles, dylibsToCache, otherDylibs, executables, couldNotLoadFiles);

    verifySelfContained(_fileSystem, dylibsToCache, otherDylibs, couldNotLoadFiles);

    // Check for required binaries before we try to build the cache
    if (!_diagnostics.hasError()) {
        // If we succeeded in building, then now see if there was a missing required file, and if so why its missing.
        std::string errorString;
        for (const LoadedMachO& dylib : otherDylibs) {
            if (dylib.inputFile->mustBeIncluded()) {
                // An error loading a required file must be propagated up to the top level diagnostic handler.
                bool gotWarning = false;
                for (const std::string& warning : dylib.inputFile->diag.warnings()) {
                    gotWarning = true;
                    std::string message = warning;
                    if (message.back() == '\n')
                        message.pop_back();
                    if (!errorString.empty())
                        errorString += "ERROR: ";
                    errorString += "Required binary was not included in the shared cache '" + std::string(dylib.inputFile->path) + "' because: " + message + "\n";
                }
                if (!gotWarning) {
                    if (!errorString.empty())
                        errorString += "ERROR: ";
                    errorString += "Required binary was not included in the shared cache '" + std::string(dylib.inputFile->path) + "' because: 'unknown error.  Please report to dyld'\n";
                }
            }
        }
        for (const LoadedMachO& dylib : couldNotLoadFiles) {
            if (dylib.inputFile->mustBeIncluded()) {
                if (dylib.inputFile->diag.hasError()) {
                    if (!errorString.empty())
                        errorString += "ERROR: ";
                    errorString += "Required binary was not included in the shared cache '" + std::string(dylib.inputFile->path) + "' because: " + dylib.inputFile->diag.errorMessage() + "\n";
                } else {
                    if (!errorString.empty())
                        errorString += "ERROR: ";
                    errorString += "Required binary was not included in the shared cache '" + std::string(dylib.inputFile->path) + "' because: 'unknown error.  Please report to dyld'\n";

                }
            }
        }
        if (!errorString.empty()) {
            _diagnostics.error("%s", errorString.c_str());
        }
    }

    if (!_diagnostics.hasError())
        build(dylibsToCache, otherDylibs, executables, aliases);

    if (!_diagnostics.hasError()) {
        // If we succeeded in building, then now see if there was a missing required file, and if so why its missing.
        std::string errorString;
        for (CacheBuilder::InputFile& inputFile : inputFiles) {
            if (inputFile.mustBeIncluded() && inputFile.diag.hasError()) {
                // An error loading a required file must be propagated up to the top level diagnostic handler.
                std::string message = inputFile.diag.errorMessage();
                if (message.back() == '\n')
                    message.pop_back();
                errorString += "Required binary was not included in the shared cache '" + std::string(inputFile.path) + "' because: " + message + "\n";
            }
        }
        if (!errorString.empty()) {
            _diagnostics.error("%s", errorString.c_str());
        }
    }

    // Add all the warnings from the input files to the top level warnings on the main diagnostics object.
    for (CacheBuilder::InputFile& inputFile : inputFiles) {
        for (const std::string& warning : inputFile.diag.warnings())
            _diagnostics.warning("%s", warning.c_str());
    }

    // Clean up the loaded files
    for (LoadedMachO& loadedMachO : dylibsToCache)
        _fileSystem.unloadFile(loadedMachO.loadedFileInfo);
    for (LoadedMachO& loadedMachO : otherDylibs)
        _fileSystem.unloadFile(loadedMachO.loadedFileInfo);
    for (LoadedMachO& loadedMachO : executables)
        _fileSystem.unloadFile(loadedMachO.loadedFileInfo);
}

void SharedCacheBuilder::build(const std::vector<DyldSharedCache::MappedMachO>& dylibs,
                               const std::vector<DyldSharedCache::MappedMachO>& otherOsDylibsInput,
                               const std::vector<DyldSharedCache::MappedMachO>& osExecutables,
                               std::vector<DyldSharedCache::FileAlias>& aliases) {

    std::vector<LoadedMachO> dylibsToCache;
    std::vector<LoadedMachO> otherDylibs;
    std::vector<LoadedMachO> executables;

    for (const DyldSharedCache::MappedMachO& mappedMachO : dylibs) {
        dyld3::closure::LoadedFileInfo loadedFileInfo;
        loadedFileInfo.fileContent      = mappedMachO.mh;
        loadedFileInfo.fileContentLen   = mappedMachO.length;
        loadedFileInfo.sliceOffset      = mappedMachO.sliceFileOffset;
        loadedFileInfo.sliceLen         = mappedMachO.length;
        loadedFileInfo.inode            = mappedMachO.inode;
        loadedFileInfo.mtime            = mappedMachO.modTime;
        loadedFileInfo.path             = mappedMachO.runtimePath.c_str();
        dylibsToCache.emplace_back((LoadedMachO){ mappedMachO, loadedFileInfo, nullptr });
    }

    for (const DyldSharedCache::MappedMachO& mappedMachO : otherOsDylibsInput) {
        dyld3::closure::LoadedFileInfo loadedFileInfo;
        loadedFileInfo.fileContent      = mappedMachO.mh;
        loadedFileInfo.fileContentLen   = mappedMachO.length;
        loadedFileInfo.sliceOffset      = mappedMachO.sliceFileOffset;
        loadedFileInfo.sliceLen         = mappedMachO.length;
        loadedFileInfo.inode            = mappedMachO.inode;
        loadedFileInfo.mtime            = mappedMachO.modTime;
        loadedFileInfo.path             = mappedMachO.runtimePath.c_str();
        otherDylibs.emplace_back((LoadedMachO){ mappedMachO, loadedFileInfo, nullptr });
    }

    for (const DyldSharedCache::MappedMachO& mappedMachO : osExecutables) {
        dyld3::closure::LoadedFileInfo loadedFileInfo;
        loadedFileInfo.fileContent      = mappedMachO.mh;
        loadedFileInfo.fileContentLen   = mappedMachO.length;
        loadedFileInfo.sliceOffset      = mappedMachO.sliceFileOffset;
        loadedFileInfo.sliceLen         = mappedMachO.length;
        loadedFileInfo.inode            = mappedMachO.inode;
        loadedFileInfo.mtime            = mappedMachO.modTime;
        loadedFileInfo.path             = mappedMachO.runtimePath.c_str();
        executables.emplace_back((LoadedMachO){ mappedMachO, loadedFileInfo, nullptr });
    }

    build(dylibsToCache, otherDylibs, executables, aliases);
}

void SharedCacheBuilder::build(const std::vector<LoadedMachO>& dylibs,
                               const std::vector<LoadedMachO>& otherOsDylibsInput,
                               const std::vector<LoadedMachO>& osExecutables,
                               std::vector<DyldSharedCache::FileAlias>& aliases)
{
    // <rdar://problem/21317611> error out instead of crash if cache has no dylibs
    // FIXME: plist should specify required vs optional dylibs
    if ( dylibs.size() < 25 ) {
        _diagnostics.error("missing required minimum set of dylibs");
        return;
    }

    _timeRecorder.pushTimedSection();

    // make copy of dylib list and sort
    makeSortedDylibs(dylibs, _options.dylibOrdering);

    _timeRecorder.recordTime("sort dylibs");

    bool impCachesSuccess = false;
    IMPCaches::HoleMap selectorAddressIntervals;
    _impCachesBuilder = new IMPCaches::IMPCachesBuilder(_sortedDylibs, _options.objcOptimizations, _diagnostics, _timeRecorder, _fileSystem);

    // Note, macOS allows install names and paths to mismatch.  This is currently not supported by
    // IMP caches as we use install names to look up the set of dylibs.
    if (    _archLayout->is64
        && (_archLayout->sharedMemorySize <= 0x100000000)
        && ((_impCachesBuilder->neededClasses.size() > 0) || (_impCachesBuilder->neededMetaclasses.size() > 0))) {
        // Build the class map across all dylibs (including cross-image superclass references)
        _impCachesBuilder->buildClassesMap(_diagnostics);

        // Determine which methods will end up in each class's IMP cache
        impCachesSuccess = _impCachesBuilder->parseDylibs(_diagnostics);

        // Compute perfect hash functions for IMP caches
        if (impCachesSuccess) _impCachesBuilder->buildPerfectHashes(selectorAddressIntervals, _diagnostics);
    }

    constexpr bool log = false;
    if (log) {
        for (const auto& p : _impCachesBuilder->selectors.map) {
            printf("0x%06x %s\n", p.second->offset, p.second->name);
        }
    }

    _timeRecorder.recordTime("compute IMP caches");

    IMPCaches::SelectorMap emptyMap;
    IMPCaches::SelectorMap& selectorMap = impCachesSuccess ? _impCachesBuilder->selectors : emptyMap;
    // assign addresses for each segment of each dylib in new cache
    parseCoalescableSegments(selectorMap, selectorAddressIntervals);
    if ( _diagnostics.hasError() )
        return;
    processSelectorStrings(osExecutables, selectorAddressIntervals);

    computeSubCaches();
    std::vector<LoadedMachO> overflowDylibs;
    while ( cacheOverflowAmount() != 0 ) {
        // IMP caches: we may need to recompute the selector addresses here to be slightly more compact
        // if we remove dylibs? This is probably overkill.

        if ( !_options.evictLeafDylibsOnOverflow ) {
            _diagnostics.error("cache overflow by %lluMB", cacheOverflowAmount() / 1024 / 1024);
            return;
        }
        size_t evictionCount = evictLeafDylibs(cacheOverflowAmount(), overflowDylibs);
        // re-layout cache
        for (DylibInfo& dylib : _sortedDylibs) {
            dylib.cacheLocation.clear();
            dylib._aslrTracker = nullptr;
        }
        _subCaches.clear();
        _coalescedText.clear();
        
        // Re-generate the hole map to remove any cruft that was added when parsing the coalescable text the first time.
        // Always clear the hole map, even if IMP caches are off, as it is used by the text coalescer
        // Construct a new HoleMap instead of cleaning it, as we want the constructor to run and take account
        // of the magic selector
        selectorAddressIntervals = IMPCaches::HoleMap();
        if (impCachesSuccess) _impCachesBuilder->computeLowBits(selectorAddressIntervals);
        
        parseCoalescableSegments(selectorMap, selectorAddressIntervals);
        if ( _diagnostics.hasError() )
            return;
        processSelectorStrings(osExecutables, selectorAddressIntervals);
        computeSubCaches();

        _diagnostics.verbose("cache overflow, evicted %lu leaf dylibs\n", evictionCount);
    }

    // allocate space used by largest possible cache plus room for LINKEDITS before optimization
    if ( _archLayout->subCacheTextLimit ) {
        // Note the 3 here is RX, RW, RO regions
        _allocatedBufferSize = (_archLayout->subCacheTextLimit * 2) * _subCaches.size() * 3 * 1.50;
    } else {
        _allocatedBufferSize = _archLayout->sharedMemorySize * 1.50;
    }

    if ( vm_allocate(mach_task_self(), &_fullAllocatedBuffer, _allocatedBufferSize, VM_FLAGS_ANYWHERE) != 0 ) {
        _diagnostics.error("could not allocate buffer");
        return;
    }

    // Now that we've allocated the buffer, go back and fix up all the addresses we allocated
    {
        for (SubCache& subCache : _subCaches) {
            subCache._readExecuteRegion.buffer += _fullAllocatedBuffer;
            for (Region& dataRegion : subCache._dataRegions) {
                dataRegion.buffer += _fullAllocatedBuffer;
                dataRegion.slideInfoBuffer += _fullAllocatedBuffer;
            }
            if ( subCache._readOnlyRegion.has_value() )
                subCache._readOnlyRegion->buffer += _fullAllocatedBuffer;
        }
        for (DylibInfo& dylib : _sortedDylibs) {
            for (SegmentMappingInfo& mappingInfo : dylib.cacheLocation)
                mappingInfo.dstSegment += _fullAllocatedBuffer;
        }
        for (const char* section: CacheCoalescedText::SupportedSections) {
            CacheCoalescedText::StringSection& cacheStringSection = _coalescedText.getSectionData(section);
            cacheStringSection.bufferAddr += _fullAllocatedBuffer;
        }
        CacheCoalescedText::CFSection& cacheSection = _coalescedText.cfStrings;
        cacheSection.bufferAddr += _fullAllocatedBuffer;

        _objcReadOnlyBuffer += _fullAllocatedBuffer;
        _objcReadWriteBuffer += _fullAllocatedBuffer;
        _swiftReadOnlyBuffer += _fullAllocatedBuffer;
    }

    markPaddingInaccessible();

     // copy all segments into cache

    unsigned long wastedSelectorsSpace = selectorAddressIntervals.totalHoleSize();
    if (wastedSelectorsSpace > 0) {
        _diagnostics.verbose("Selector placement for IMP caches wasted %lu bytes\n", wastedSelectorsSpace);
        if (log) {
            std::cerr << selectorAddressIntervals << std::endl;
        }
    }

    _timeRecorder.recordTime("layout cache");

    writeCacheHeader();
    copyRawSegments();
    _timeRecorder.recordTime("copy cached dylibs into buffer");

    // rebase all dylibs for new location in cache
    for (SubCache& subCache : _subCaches) {
        if ( subCache._dataRegions.empty() )
            continue;
        subCache._aslrTracker.setDataRegion(subCache.firstDataRegion()->buffer, subCache.dataRegionsTotalSize());
        if ( !_options.cacheSupportsASLR )
            subCache._aslrTracker.disable();
    }
    adjustAllImagesForNewSegmentLocations(_archLayout->sharedMemoryStart, &_lohTracker, &_coalescedText);
    if ( _diagnostics.hasError() )
        return;

    _timeRecorder.recordTime("adjust segments for new split locations");

    // find a typical main executable for use during dylib binding
    const MachOAnalyzer* aMainExecutable = nullptr;
    if ( !_options.forSimulator ) {
        const char* binPath = "/usr/bin/";
        if ( _options.platform == dyld3::Platform::driverKit )
            binPath = "/System/Library/DriverExtensions/";
        size_t binPathLen = strlen(binPath);
        for ( const LoadedMachO& anExe : osExecutables ) {
            if ( strncmp(anExe.inputFile->path, binPath, binPathLen) == 0 ) {
                aMainExecutable = (dyld3::MachOAnalyzer*)anExe.loadedFileInfo.fileContent;
            }
        }
    } else {
        // HACK: use libSystem.dylib from cache as main executable to bootstrap state
        // We should remove this once the sim builds dyld4 loaders for executables.
        // See commented out code in addIfMachO() for adding to mainExecutables set
        for ( const LoadedMachO& dylib : dylibs ) {
            const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)dylib.loadedFileInfo.fileContent;
            if ( strcmp(ma->installName(), "/usr/lib/libSystem.B.dylib") == 0 ) {
                aMainExecutable = ma;
                break;
            }
        }
    }

    if ( !aMainExecutable ) {
        _diagnostics.error("Could not find a main executable for building cache loaders");
        return;
    }

    // build JustInTimeLoaders for all dylibs in cache and bind them together
    bindDylibs(aMainExecutable, aliases);
    if ( _diagnostics.hasError() )
        return;

    _timeRecorder.recordTime("bind all images");

    if ( _options.platform != dyld3::Platform::driverKit ) {
        // optimize ObjC (now that all dylibs are in place and all binds/rebases are resolved)
        optimizeObjC(impCachesSuccess, _impCachesBuilder->inlinedSelectors);
    }

    delete _impCachesBuilder;
    _impCachesBuilder = nullptr;

    if ( _diagnostics.hasError() )
        return;

    _timeRecorder.recordTime("optimize Objective-C");

    optimizeSwift();
    if ( _diagnostics.hasError() )
        return;

    _timeRecorder.recordTime("optimize Swift");

    if ( _options.optimizeStubs ) {
        DyldSharedCache* dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;

        __block std::vector<std::pair<const mach_header*, const char*>> images;
        dyldCache->forEachImage(^(const mach_header *mh, const char *installName) {
            images.push_back({ mh, installName });
        });

        int64_t cacheSlide = (long)dyldCache - dyldCache->unslidLoadAddress();
        uint64_t cacheUnslideAddr = dyldCache->unslidLoadAddress();
        optimizeAwayStubs(images, cacheSlide, cacheUnslideAddr,
                          dyldCache, _s_neverStubEliminateSymbols);
    }


    // FIPS seal corecrypto, This must be done after stub elimination (so that __TEXT,__text is not changed after sealing)
    fipsSign();

    _timeRecorder.recordTime("do stub elimination");

    // merge and compact LINKEDIT segments
    {
        DyldSharedCache* dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;

        // If we want to remove, not just unmap locals, then set the dylibs themselves to be stripped
        DylibStripMode dylibStripMode = DylibStripMode::stripNone;
        if ( _options.localSymbolMode == DyldSharedCache::LocalSymbolsMode::strip )
            dylibStripMode = CacheBuilder::DylibStripMode::stripLocals;

        UnmappedLocalsOptimizer* localsOptimizer = createLocalsOptimizer(_sortedDylibs.size());

        // Optimize each subcache individually
        for (SubCache& subCache : _subCaches) {
            // Skip subCache's which don't contain LINKEDIT
            if ( subCache._linkeditNumDylibs == 0 )
                continue;

            assert(subCache._readOnlyRegion.has_value());
            
            dyld3::Array<DylibInfo> cacheImages(_sortedDylibs.data(), _sortedDylibs.size(), _sortedDylibs.size());
            dyld3::Array<DylibInfo> subCacheImages = cacheImages.subArray(subCache._linkeditFirstDylibIndex, subCache._linkeditNumDylibs);

            // Work out which images are in this subcache.
            std::unordered_set<std::string_view> subCacheInstallNames;
            for (const DylibInfo& dylib : subCacheImages) {
                subCacheInstallNames.insert(dylib.input->mappedFile.mh->installName());
            }

            __block std::vector<std::tuple<const mach_header*, const char*, DylibStripMode>> images;
            dyldCache->forEachImage(^(const mach_header *mh, const char *installName) {
                if ( subCacheInstallNames.count(installName) != 0 )
                    images.push_back({ mh, installName, dylibStripMode });
            });
            assert(!images.empty());
            optimizeLinkedit(*(subCache._readOnlyRegion), subCache._nonLinkEditReadOnlySize, localsOptimizer, images);
        }

        if ( _options.localSymbolMode == DyldSharedCache::LocalSymbolsMode::unmap )
            emitLocalSymbols(localsOptimizer);

        destroyLocalsOptimizer(localsOptimizer);

        // Make a subCache for the local symbols file
        if ( _localSymbolsRegion.buffer != nullptr ) {
            // Add a page-sized header and LINKEDIT.  As this is just the symbols file, we'll just use 16k pages
            const uint32_t pageSize = 16384;
            _localSymbolsSubCacheBuffer.resize(pageSize * 3);
            _localSymbolsSubCache._readExecuteRegion.buffer               = (uint8_t*)_localSymbolsSubCacheBuffer.data();
            _localSymbolsSubCache._readExecuteRegion.bufferSize           = pageSize;
            _localSymbolsSubCache._readExecuteRegion.sizeInUse            = pageSize;
            _localSymbolsSubCache._readExecuteRegion.unslidLoadAddress    = 0;
            _localSymbolsSubCache._readExecuteRegion.cacheFileOffset      = 0;
            _localSymbolsSubCache._readExecuteRegion.initProt             = VM_PROT_READ | VM_PROT_EXECUTE;
            _localSymbolsSubCache._readExecuteRegion.maxProt              = VM_PROT_READ | VM_PROT_EXECUTE;
            _localSymbolsSubCache._readExecuteRegion.name                 = "__TEXT";

            writeSharedCacheHeader(_localSymbolsSubCache, _options, *_archLayout, 0, 0, 0, 0);
        }
    }

    // rebuild JIT loaders for all dylibs, then serialize them to a PrebuiltLoaderSet and append that to the read-only region
    buildDylibsPrebuiltLoaderSet(aMainExecutable, aliases);
    if ( _diagnostics.hasError() )
        return;

    _timeRecorder.recordTime("optimize LINKEDITs");

    // don't add dyld3 closures to simulator cache or the base system where size is more of an issue
    if ( _options.optimizeDyldLaunches ) {
        // compute and add launch closures to end of read-only region
        buildLaunchSets(osExecutables, otherOsDylibsInput, overflowDylibs);
        if ( _diagnostics.hasError() )
            return;
    } else {
        // We didn't optimize launches, but we still need to align the LINKEDIT as that was normally
        // done at the end of buildLaunchSets(), and the LINKEDIT from buildDylibsPrebuiltLoaderSet() was
        // not aligned
        for (SubCache& subCache : _subCaches) {
            // Skip subCaches without read only regions
            if ( !subCache._readOnlyRegion.has_value() )
                continue;
            subCache._readOnlyRegion->sizeInUse = align(subCache._readOnlyRegion->sizeInUse, 14);
        }
    }

    // update final readOnly region size
    // Note each subcache has its own cache header
    for (SubCache& subCache : _subCaches) {
        // Skip subCaches without read only regions
        if ( !subCache._readOnlyRegion.has_value() )
            continue;
        DyldSharedCache* dyldCache = (DyldSharedCache*)subCache._readExecuteRegion.buffer;
        dyld_cache_mapping_info* mappings = (dyld_cache_mapping_info*)(subCache._readExecuteRegion.buffer + dyldCache->header.mappingOffset);
        mappings[dyldCache->header.mappingCount - 1].size = subCache._readOnlyRegion->sizeInUse;
        dyld_cache_mapping_and_slide_info* slidableMappings = (dyld_cache_mapping_and_slide_info*)(subCache._readExecuteRegion.buffer + dyldCache->header.mappingWithSlideOffset);
        slidableMappings[dyldCache->header.mappingCount - 1].size = subCache._readOnlyRegion->sizeInUse;

        // Update Rosetta read-only range, which starts at the end of LINKEDIT
        if ( subCache._rosettaReadOnlySize != 0 ) {
            uint64_t rosettaEndAddress = subCache._rosettaReadOnlyAddr + subCache._rosettaReadOnlySize;
            subCache._rosettaReadOnlyAddr = subCache._readOnlyRegion->unslidLoadAddress + subCache._readOnlyRegion->sizeInUse;
            assert(subCache._rosettaReadOnlyAddr < rosettaEndAddress);
            subCache._rosettaReadOnlySize = rosettaEndAddress - subCache._rosettaReadOnlyAddr;

            // Update the cache header too
            dyldCache->header.rosettaReadOnlyAddr   = subCache._rosettaReadOnlyAddr;
            dyldCache->header.rosettaReadOnlySize   = subCache._rosettaReadOnlySize;
        }
    }

    // Update the final shared region size.  This holds the exact size we need to allocate to hold all subcaches
    {
        const SubCache& firstSubCache = _subCaches.front();
        const SubCache& lastSubCache = _subCaches.back();
        uint64_t vmSize = lastSubCache.highestVMAddress() - firstSubCache._readExecuteRegion.unslidLoadAddress;
        DyldSharedCache* dyldCache = (DyldSharedCache*)firstSubCache._readExecuteRegion.buffer;
        dyldCache->header.sharedRegionSize = vmSize;
    }

    // If we have unmapped locals, they are in their own file.  This allows customer and dev caches to share the file
    if ( _localSymbolsRegion.sizeInUse != 0 ) {
        SubCache& subCache = _localSymbolsSubCache;
        DyldSharedCache* dyldCache = (DyldSharedCache*)subCache._readExecuteRegion.buffer;
        dyldCache->header.localSymbolsOffset = subCache._readExecuteRegion.cacheFileOffset + subCache._readExecuteRegion.sizeInUse;
        dyldCache->header.localSymbolsSize   = _localSymbolsRegion.sizeInUse;
    }

    // record max slide now that final size is established
    if ( _options.cacheSupportsASLR ) {
        DyldSharedCache* dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;
        dyldCache->header.maxSlide = ~0ULL;

        if ( _archLayout->sharedRegionsAreDiscontiguous ) {
            if ( _options.forSimulator || startsWith(_archLayout->archName, "small-") ) {
                // The x86_64 simulators back deploy, and so uses the magic values from the previous OS

                // special case x86_64 which has three non-contiguous chunks each in their own 1GB regions
                assert(_subCaches.size() == 1);
                const SubCache& subCache = _subCaches.front();
                uint64_t maxSlide0 = SIM_DISCONTIGUOUS_RX_SIZE - subCache._readExecuteRegion.sizeInUse; // TEXT region has 1.5GB region
                uint64_t maxSlide1 = SIM_DISCONTIGUOUS_RW_SIZE - subCache.dataRegionsTotalSize();
                uint64_t maxSlide2 = SIM_DISCONTIGUOUS_RO_SIZE - subCache._readOnlyRegion->sizeInUse;
                dyldCache->header.maxSlide = std::min(std::min(maxSlide0, maxSlide1), maxSlide2);

                // What we computed above is the macOS 11 max slide.
                // macOS 11 has the shared cache at a relatively high address with 3.5GB of size.
                // macOS 12 has the cache at a lower address with 32GB of size
                // A cache built against the macOS 11 numbers might actually overflow when running on macOS 12.  We need to limit it even more
                const uint64_t SHARED_REGION_BASE_X86_64_MACOS12 = 0x00007FF800000000ULL;
                const uint64_t SHARED_REGION_SIZE_X86_64_MACOS12 = 0x00000007FE000000ULL;
                uint64_t macOS12MaxVMAddress = SHARED_REGION_BASE_X86_64_MACOS12 + SHARED_REGION_SIZE_X86_64_MACOS12;
                uint64_t maxVMAddress = subCache.highestVMAddress();
                uint64_t maxOS12Slide = macOS12MaxVMAddress - maxVMAddress;
                dyldCache->header.maxSlide = std::min(dyldCache->header.maxSlide, maxOS12Slide);
            } else {
                // Large x86_64 caches.  All TEXT/DATA/LINKEDIT are on their own 1GB ranges.  The max slide keeps them within their ranges
                // TODO: Check if we can just slide these arbitrarily within the VM space, now that thair slid ranges will always be
                // on 1GB boundaries.
                for (SubCache& subCache : _subCaches) {
                    // TEXT
                    dyldCache->header.maxSlide = std::min(dyldCache->header.maxSlide, DISCONTIGUOUS_REGION_SIZE - subCache._readExecuteRegion.sizeInUse);
                    // DATA
                    dyldCache->header.maxSlide = std::min(dyldCache->header.maxSlide, DISCONTIGUOUS_REGION_SIZE - subCache.dataRegionsTotalSize());
                    // LINKEDIT
                    dyldCache->header.maxSlide = std::min(dyldCache->header.maxSlide, DISCONTIGUOUS_REGION_SIZE - subCache._readOnlyRegion->sizeInUse);
                }
            }
        } else {
            uint64_t maxVMAddress = _subCaches.back().highestVMAddress();
            dyldCache->header.maxSlide = (_archLayout->sharedMemoryStart + _archLayout->sharedMemorySize) - maxVMAddress;

            // <rdar://problem/49852839> branch predictor on arm64 currently only looks at low 32-bits, so don't slide cache more than 2GB
            if ( _archLayout->sharedMemorySize == 0x100000000 ) {
                if ( _archLayout->useSplitCacheLayout || _archLayout->subCacheTextLimit ) {
                    // Split cache __TEXT is contiguous from the first subCache until the last
                    // Large caches might get lucky and have the last __TEXT end before 2GB
                    const SubCache& firstSubCache = _subCaches.front();
                    const SubCache& lastSubCache = _subCaches.back();
                    uint64_t textVMSize = lastSubCache._readExecuteRegion.sizeInUse + (lastSubCache._readExecuteRegion.unslidLoadAddress - firstSubCache._readExecuteRegion.unslidLoadAddress);
                    if ( textVMSize < 0x80000000ULL )
                        dyldCache->header.maxSlide = std::min(dyldCache->header.maxSlide, 0x80000000ULL - textVMSize);
                } else {
                    // Single arm64e cache file
                    assert(_subCaches.size() == 1);
                    const SubCache& subCache = _subCaches.front();
                    if ( subCache._readExecuteRegion.sizeInUse < 0x80000000ULL )
                        dyldCache->header.maxSlide = std::min(dyldCache->header.maxSlide, 0x80000000ULL - subCache._readExecuteRegion.sizeInUse);
                }
            }
        }
    }

    // mark if any input dylibs were built with chained fixups
    {
        DyldSharedCache* dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;
        dyldCache->header.builtFromChainedFixups = _someDylibsUsedChainedFixups;
    }

    _timeRecorder.recordTime("build %lu closures", osExecutables.size());
    // Emit the CF strings without their ISAs being signed
    // This must be after buildDylibsPrebuiltLoaderSet() as it depends on hasImageIndex().
    // It also has to be before emitting slide info as it adds ASLR entries.
    // Disabled until we see if we need this.
    // emitContantObjects();

    _timeRecorder.recordTime("emit constant objects");

    // fill in slide info at start of region[2]
    // do this last because it modifies pointers in DATA segments
    if ( _options.cacheSupportsASLR ) {
        for (SubCache& subCache : _subCaches) {
            // Skip subCache's without __DATA segments
            if ( subCache._dataRegions.empty() )
                continue;
            if ( strcmp(_archLayout->archName, "arm64e") == 0 ) {
                writeSlideInfoV3(subCache);
            } else if ( _archLayout->is64 )
                writeSlideInfoV2<Pointer64<LittleEndian>>(subCache);
            else if ( _archLayout->pointerDeltaMask == 0xC0000000 )
                writeSlideInfoV4<Pointer32<LittleEndian>>(subCache);
            else
                writeSlideInfoV2<Pointer32<LittleEndian>>(subCache);
        }
    }

    _timeRecorder.recordTime("compute slide info");

    // last sanity check on size
    {
        const SubCache* overflowingSubCache = nullptr;
        if ( cacheOverflowAmount(&overflowingSubCache) != 0 ) {
            _diagnostics.error("cache overflow after optimizations 0x%llX -> 0x%llX",
                               overflowingSubCache->_readExecuteRegion.unslidLoadAddress, overflowingSubCache->highestVMAddress());
            return;
        }
    }

    // codesignature is part of file, but is not mapped
    if ( _localSymbolsRegion.sizeInUse != 0 ) {
        codeSign(_localSymbolsSubCache);
        DyldSharedCache* dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;
        memcpy(dyldCache->header.symbolFileUUID, ((DyldSharedCache*)_localSymbolsSubCache._readExecuteRegion.buffer)->header.uuid, 16);
    }
    if ( _subCaches.size() > 1 ) {
        // Codesign the subcaches first, as then we can add their UUIDS to the main cache header
        DyldSharedCache* dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;
        dyld_subcache_entry* subCacheEntries = (dyld_subcache_entry*)((uint8_t*)dyldCache + dyldCache->header.subCacheArrayOffset);

        for (uint64_t i = 1, e = _subCaches.size(); i != e; ++i) {
            SubCache& subCache = _subCaches[i];
            codeSign(subCache);
            if ( _diagnostics.hasError() )
                return;

            assert(i <= dyldCache->header.subCacheArrayCount);
            memcpy(subCacheEntries[i - 1].uuid, ((DyldSharedCache*)subCache._readExecuteRegion.buffer)->header.uuid, 16);
        }
    }
    codeSign(_subCaches.front());

    _timeRecorder.recordTime("compute UUID and codesign cache file");

    if (_options.verbose) {
        _timeRecorder.logTimings();
    }

    return;
}

const std::set<std::string> SharedCacheBuilder::warnings()
{
    return _diagnostics.warnings();
}

const std::set<const dyld3::MachOAnalyzer*> SharedCacheBuilder::evictions()
{
    return _evictions;
}

void SharedCacheBuilder::deleteBuffer()
{
    // Cache buffer
    if ( _allocatedBufferSize != 0 ) {
        vm_deallocate(mach_task_self(), _fullAllocatedBuffer, _allocatedBufferSize);
        _fullAllocatedBuffer = 0;
        _allocatedBufferSize = 0;
    }
    // Local symbols buffer
    if ( _localSymbolsRegion.bufferSize != 0 ) {
        vm_deallocate(mach_task_self(), (vm_address_t)_localSymbolsRegion.buffer, _localSymbolsRegion.bufferSize);
        _localSymbolsRegion.buffer = 0;
        _localSymbolsRegion.bufferSize = 0;
    }
    // Code signatures
    for (SubCache& subCache : _subCaches) {
        if ( subCache._codeSignatureRegion.bufferSize != 0 ) {
            vm_deallocate(mach_task_self(), (vm_address_t)subCache._codeSignatureRegion.buffer, subCache._codeSignatureRegion.bufferSize);
            subCache._codeSignatureRegion.buffer = 0;
            subCache._codeSignatureRegion.bufferSize = 0;
        }
    }
}


void SharedCacheBuilder::makeSortedDylibs(const std::vector<LoadedMachO>& dylibs, const std::unordered_map<std::string, unsigned> sortOrder)
{
    for (const LoadedMachO& dylib : dylibs) {
        _sortedDylibs.push_back({ { &dylib, dylib.mappedFile.runtimePath, {}, {} }, {}, {} });
    }

    std::sort(_sortedDylibs.begin(), _sortedDylibs.end(), [&](const DylibInfo& a, const DylibInfo& b) {
        const auto& orderA = sortOrder.find(a.input->mappedFile.runtimePath);
        const auto& orderB = sortOrder.find(b.input->mappedFile.runtimePath);
        bool foundA = (orderA != sortOrder.end());
        bool foundB = (orderB != sortOrder.end());

        // Order all __DATA_DIRTY segments specified in the order file first, in
        // the order specified in the file, followed by any other __DATA_DIRTY
        // segments in lexicographic order.
        if ( foundA && foundB )
            return orderA->second < orderB->second;
        else if ( foundA )
            return true;
        else if ( foundB )
             return false;

        // Sort mac before iOSMac
        bool isIOSMacA = strncmp(a.input->mappedFile.runtimePath.c_str(), "/System/iOSSupport/", 19) == 0;
        bool isIOSMacB = strncmp(b.input->mappedFile.runtimePath.c_str(), "/System/iOSSupport/", 19) == 0;
        if (isIOSMacA != isIOSMacB)
            return !isIOSMacA;

        // Finally sort by path
        return a.input->mappedFile.runtimePath < b.input->mappedFile.runtimePath;
    });
}

struct DylibAndSize
{
    const CacheBuilder::LoadedMachO*    input;
    const char*                         installName;
    uint64_t                            size;
};

uint64_t SharedCacheBuilder::cacheOverflowAmount(const SubCache** overflowingSubCache)
{
    if ( _archLayout->sharedRegionsAreDiscontiguous ) {
        // The x86_64 simulator back deploys, and so uses the magic values from the previous OS
        if ( _options.forSimulator || startsWith(_archLayout->archName, "small-") ) {
            assert(_subCaches.size() == 1);
            const SubCache& subCache = _subCaches.front();

            // for macOS x86_64 cache, need to check each region for overflow
            if ( subCache._readExecuteRegion.sizeInUse > SIM_DISCONTIGUOUS_RX_SIZE )
                return (subCache._readExecuteRegion.sizeInUse - SIM_DISCONTIGUOUS_RX_SIZE);

            uint64_t dataSize = subCache.dataRegionsTotalSize();
            if ( dataSize > SIM_DISCONTIGUOUS_RW_SIZE )
                return (dataSize - SIM_DISCONTIGUOUS_RW_SIZE);

            assert(subCache._readOnlyRegion.has_value());
            if ( subCache._readOnlyRegion->sizeInUse > SIM_DISCONTIGUOUS_RO_SIZE )
                return (subCache._readOnlyRegion->sizeInUse - SIM_DISCONTIGUOUS_RO_SIZE);

            // No overflow
            return 0;
        }
        for (const SubCache& subCache : _subCaches) {
            // for macOS x86_64 cache, need to check each region for overflow
            if ( subCache._readExecuteRegion.sizeInUse > DISCONTIGUOUS_REGION_SIZE ) {
                if ( overflowingSubCache != nullptr )
                    *overflowingSubCache = &subCache;
                return (subCache._readExecuteRegion.sizeInUse - DISCONTIGUOUS_REGION_SIZE);
            }

            uint64_t dataSize = subCache.dataRegionsTotalSize();
            if ( dataSize > DISCONTIGUOUS_REGION_SIZE ) {
                if ( overflowingSubCache != nullptr )
                    *overflowingSubCache = &subCache;
                return (dataSize - DISCONTIGUOUS_REGION_SIZE);
            }

            if ( subCache._readOnlyRegion.has_value() ) {
                if ( subCache._readOnlyRegion->sizeInUse > DISCONTIGUOUS_REGION_SIZE ) {
                    if ( overflowingSubCache != nullptr )
                        *overflowingSubCache = &subCache;
                    return (subCache._readOnlyRegion->sizeInUse - DISCONTIGUOUS_REGION_SIZE);
                }
            }
        }
    }
    // For x86_64, fall through to the check that the overall cache size isn't too big
    // This is unlikely given that the limit is 32GB.
    {
        const SubCache& firstSubCache = _subCaches.front();
        const SubCache& lastSubCache = _subCaches.back();
        uint64_t vmSize = lastSubCache.highestVMAddress() - firstSubCache._readExecuteRegion.unslidLoadAddress;

        // If we have LINKEDIT, then it might get optimized.  In early calls to this function, we estimate how much
        // it might be optimized by.  In later calls its already optimized
        if ( lastSubCache._readOnlyRegion.has_value() ) {
            const Region& readOnlyRegion = *lastSubCache._readOnlyRegion;
            bool alreadyOptimized = (readOnlyRegion.sizeInUse != readOnlyRegion.bufferSize);

            // The earlier vmSize calculation already included the current size of the readOnlyRegion.
            // If we have passed the LINKEDIT optimizer, then that is already an accurate size.
            // Otherwise estimate a new size
            if ( !alreadyOptimized ) {
                // Estimate a new size.  First subtract the existing LINKEDIT size
                vmSize -= readOnlyRegion.sizeInUse;
                if ( _options.localSymbolMode == DyldSharedCache::LocalSymbolsMode::unmap ) {
                    // assume locals removal and LINKEDIT optimzation reduces LINKEDITs %25 of original size
                    // Note, its really more like 20%, but we need some of the saved space for patch tables, PBLS, etc.
                    // So use 25% to give us a little room for those data structures
                    vmSize += (readOnlyRegion.sizeInUse * 25/100);
                } else {
                    // assume LINKEDIT optimzation reduces LINKEDITs to %80 of original size
                    vmSize += (readOnlyRegion.sizeInUse * 80/100);
                }
            }
        }
        if ( vmSize > _archLayout->sharedMemorySize ) {
            if ( overflowingSubCache != nullptr )
                *overflowingSubCache = &lastSubCache;
            return vmSize - _archLayout->sharedMemorySize;
        }
    }

    // Finally, check that 2GB offsets in exception handling don't overflow.  For now this limits TEXT+DATA to 2GB
    // Note this does not apply to large caches, as they have their own TEXT+DATA in a shorter range
    if ( !_archLayout->sharedRegionsAreDiscontiguous
        && ((_archLayout->subCacheTextLimit == 0) || _archLayout->useSplitCacheLayout) ) {
        const SubCache& firstSubCache = _subCaches.front();
        const SubCache& lastSubCache = _subCaches.back();
        if ( const Region* lastDataRegion = lastSubCache.lastDataRegion() ) {
            uint64_t vmSizeTextData = (lastDataRegion->unslidLoadAddress + lastDataRegion->sizeInUse) - firstSubCache._readExecuteRegion.unslidLoadAddress;
            const uint64_t twoGB = 1ULL << 31;
            if ( vmSizeTextData > twoGB ) {
                if ( overflowingSubCache != nullptr )
                    *overflowingSubCache = &lastSubCache;
                return vmSizeTextData - twoGB;
            }
        }
    }
    // fits in shared region
    return 0;
}

size_t SharedCacheBuilder::evictLeafDylibs(uint64_t reductionTarget, std::vector<LoadedMachO>& overflowDylibs)
{
    // build a reverse map of all dylib dependencies
    __block std::map<std::string, std::set<std::string>> references;
    std::map<std::string, std::set<std::string>>* referencesPtr = &references;
    for (const DylibInfo& dylib : _sortedDylibs) {
        // Esnure we have an entry (even if it is empty)
        if (references.count(dylib.input->mappedFile.mh->installName()) == 0) {
            references[dylib.input->mappedFile.mh->installName()] = std::set<std::string>();
        };
        dylib.input->mappedFile.mh->forEachDependentDylib(^(const char* loadPath, bool isWeak, bool isReExport, bool isUpward, uint32_t compatVersion, uint32_t curVersion, bool &stop) {
            references[loadPath].insert(dylib.input->mappedFile.mh->installName());
        });
    }

    // Find the sizes of all the dylibs
    std::vector<DylibAndSize> dylibsToSort;
    std::vector<DylibAndSize> sortedDylibs;
    for (const DylibInfo& dylib : _sortedDylibs) {
        const char* installName = dylib.input->mappedFile.mh->installName();
        __block uint64_t segsSize = 0;
        dylib.input->mappedFile.mh->forEachSegment(^(const dyld3::MachOFile::SegmentInfo& info, bool& stop) {
            if ( strcmp(info.segName, "__LINKEDIT") != 0 )
                segsSize += info.vmSize;
        });
        dylibsToSort.push_back({ dylib.input, installName, segsSize });
    }

    // Build an ordered list of what to remove. At each step we do following
    // 1) Find all dylibs that nothing else depends on
    // 2a) If any of those dylibs are not in the order select the largest one of them
    // 2b) If all the leaf dylibs are in the order file select the last dylib that appears last in the order file
    // 3) Remove all entries to the removed file from the reverse dependency map
    // 4) Go back to one and repeat until there are no more evictable dylibs
    // This results in us always choosing the locally optimal selection, and then taking into account how that impacts
    // the dependency graph for subsequent selections

    bool candidateFound = true;
    while (candidateFound) {
        candidateFound = false;
        DylibAndSize candidate;
        uint64_t candidateOrder = 0;
        for(const auto& dylib : dylibsToSort) {
            const auto& i = referencesPtr->find(dylib.installName);
            assert(i != referencesPtr->end());
            if (!i->second.empty()) {
                continue;
            }
            const auto& j = _options.dylibOrdering.find(dylib.input->mappedFile.runtimePath);
            uint64_t order = 0;
            if (j != _options.dylibOrdering.end()) {
                order = j->second;
            } else {
                // Not in the order file, set order sot it goes to the front of the list
                order = UINT64_MAX;
            }
            if (order > candidateOrder ||
                (order == UINT64_MAX && candidate.size < dylib.size)) {
                    // The new file is either a lower priority in the order file
                    // or the same priority as the candidate but larger
                    candidate = dylib;
                    candidateOrder = order;
                    candidateFound = true;
            }
        }
        if (candidateFound) {
            sortedDylibs.push_back(candidate);
            referencesPtr->erase(candidate.installName);
            for (auto& dependent : references) {
                (void)dependent.second.erase(candidate.installName);
            }
            auto j = std::find_if(dylibsToSort.begin(), dylibsToSort.end(), [&candidate](const DylibAndSize& dylib) {
                return (strcmp(candidate.installName, dylib.installName) == 0);
            });
            if (j != dylibsToSort.end()) {
                dylibsToSort.erase(j);
            }
        }
    }

     // build set of dylibs that if removed will allow cache to build
    for (DylibAndSize& dylib : sortedDylibs) {
        if ( _options.verbose )
            _diagnostics.warning("to prevent cache overflow, not caching %s", dylib.installName);
        _evictions.insert(dylib.input->mappedFile.mh);
        // Track the evicted dylibs so we can try build "other" dlopen closures for them.
        overflowDylibs.push_back(*dylib.input);
        if ( dylib.size > reductionTarget )
            break;
        reductionTarget -= dylib.size;
    }

    // prune _sortedDylibs
    _sortedDylibs.erase(std::remove_if(_sortedDylibs.begin(), _sortedDylibs.end(), [&](const DylibInfo& dylib) {
        return (_evictions.count(dylib.input->mappedFile.mh) != 0);
    }),_sortedDylibs.end());

    return _evictions.size();
}

void SharedCacheBuilder::writeSharedCacheHeader(const SubCache& subCache, const DyldSharedCache::CreateOptions& options,
                                                const ArchLayout& layout,
                                                uint32_t osVersion, uint32_t altPlatform, uint32_t altOsVersion,
                                                uint64_t cacheType)
{
    // "dyld_v1" + spaces + archName(), with enough spaces to pad to 15 bytes
    std::string magic = "dyld_v1";
    magic.append(15 - magic.length() - strlen(options.archs->name()), ' ');
    magic.append(options.archs->name());
    assert(magic.length() == 15);

    // 1 __TEXT segment, n __DATA segments, and 0/1 __LINKEDIT segment
    const uint32_t mappingCount = 1 + (uint32_t)subCache._dataRegions.size() + (subCache._readOnlyRegion.has_value() ? 1 : 0);
    assert(mappingCount <= DyldSharedCache::MaxMappings);

    // fill in header
    dyld_cache_header* dyldCacheHeader = (dyld_cache_header*)subCache._readExecuteRegion.buffer;
    memcpy(dyldCacheHeader->magic, magic.c_str(), 16);
    dyldCacheHeader->mappingOffset        = sizeof(dyld_cache_header);
    dyldCacheHeader->mappingCount         = mappingCount;
    dyldCacheHeader->mappingWithSlideOffset = (uint32_t)(dyldCacheHeader->mappingOffset + mappingCount*sizeof(dyld_cache_mapping_and_slide_info));
    dyldCacheHeader->mappingWithSlideCount  = mappingCount;
    dyldCacheHeader->imagesOffsetOld      = 0;
    dyldCacheHeader->imagesCountOld       = 0;
    dyldCacheHeader->imagesOffset      = 0;
    dyldCacheHeader->imagesCount       = 0;
    dyldCacheHeader->dyldBaseAddress      = 0;
    dyldCacheHeader->codeSignatureOffset  = 0;
    dyldCacheHeader->codeSignatureSize    = 0;
    dyldCacheHeader->slideInfoOffsetUnused     = 0;
    dyldCacheHeader->slideInfoSizeUnused       = 0;
    dyldCacheHeader->localSymbolsOffset   = 0;
    dyldCacheHeader->localSymbolsSize     = 0;
    dyldCacheHeader->cacheType            = cacheType;
    dyldCacheHeader->accelerateInfoAddr   = 0;
    dyldCacheHeader->accelerateInfoSize   = 0;
    bzero(dyldCacheHeader->uuid, 16);// overwritten later by recomputeCacheUUID()
    dyldCacheHeader->branchPoolsOffset    = 0;
    dyldCacheHeader->branchPoolsCount     = 0;
    dyldCacheHeader->imagesTextOffset     = 0;
    dyldCacheHeader->imagesTextCount      = 0;
    dyldCacheHeader->patchInfoAddr        = 0;
    dyldCacheHeader->patchInfoSize        = 0;
    dyldCacheHeader->otherImageGroupAddrUnused  = 0;
    dyldCacheHeader->otherImageGroupSizeUnused  = 0;
    dyldCacheHeader->progClosuresAddr     = 0;
    dyldCacheHeader->progClosuresSize     = 0;
    dyldCacheHeader->progClosuresTrieAddr = 0;
    dyldCacheHeader->progClosuresTrieSize = 0;
    dyldCacheHeader->platform             = (uint8_t)options.platform;
    dyldCacheHeader->formatVersion        = 0; //dyld3::closure::kFormatVersion;
    dyldCacheHeader->dylibsExpectedOnDisk = !options.dylibsRemovedDuringMastering;
    dyldCacheHeader->simulator            = options.forSimulator;
    dyldCacheHeader->locallyBuiltCache    = options.isLocallyBuiltCache;
    dyldCacheHeader->builtFromChainedFixups= false;
    dyldCacheHeader->sharedRegionStart    = subCache._readExecuteRegion.unslidLoadAddress;
    dyldCacheHeader->sharedRegionSize     = 0;
    dyldCacheHeader->maxSlide             = 0; // overwritten later in build if the cache supports ASLR
    dyldCacheHeader->dylibsImageArrayAddr       = 0; // no longer used
    dyldCacheHeader->dylibsImageArraySize       = 0; // no longer used
    dyldCacheHeader->dylibsTrieAddr             = 0; // no longer used
    dyldCacheHeader->dylibsTrieSize             = 0; // no longer used
    dyldCacheHeader->otherImageArrayAddr        = 0; // no longer used
    dyldCacheHeader->otherImageArraySize        = 0; // no longer used
    dyldCacheHeader->otherTrieAddr              = 0; // no longer used
    dyldCacheHeader->otherTrieSize              = 0; // no longer used
    dyldCacheHeader->dylibsPBLStateArrayAddrUnused    = 0; // no longer used
    dyldCacheHeader->dylibsPBLSetAddr           = 0;
    dyldCacheHeader->programTrieAddr            = 0;
    dyldCacheHeader->programTrieSize            = 0;
    dyldCacheHeader->osVersion                  = osVersion;
    dyldCacheHeader->altPlatform                = altPlatform;
    dyldCacheHeader->altOsVersion               = altOsVersion;
    dyldCacheHeader->swiftOptsOffset            = 0;
    dyldCacheHeader->swiftOptsSize              = 0;
    dyldCacheHeader->subCacheArrayOffset        = 0;
    dyldCacheHeader->subCacheArrayCount         = 0;
    bzero(dyldCacheHeader->symbolFileUUID, 16);      // overwritten later after measuring the local symbols file
    dyldCacheHeader->rosettaReadOnlyAddr        = subCache._rosettaReadOnlyAddr;
    dyldCacheHeader->rosettaReadOnlySize        = subCache._rosettaReadOnlySize;
    dyldCacheHeader->rosettaReadWriteAddr       = subCache._rosettaReadWriteAddr;
    dyldCacheHeader->rosettaReadWriteSize       = subCache._rosettaReadWriteSize;

    // fill in mappings
    dyld_cache_mapping_info* mappings = (dyld_cache_mapping_info*)(subCache._readExecuteRegion.buffer + dyldCacheHeader->mappingOffset);
    assert(subCache._readExecuteRegion.cacheFileOffset == 0);

    uint32_t firstMappingProt = VM_PROT_READ | VM_PROT_EXECUTE;
    // In the LINKEDIT only sub cache, hack TEXT to also be RO, as then it shares the same protections
    // as the subsequent LINKEDIT region
    if ( (subCache._textNumDylibs == 0) && (subCache._dataNumDylibs == 0) && (subCache._linkeditNumDylibs != 0) ) {
        firstMappingProt = VM_PROT_READ;
    }

    mappings[0].address    = subCache._readExecuteRegion.unslidLoadAddress;
    mappings[0].fileOffset = subCache._readExecuteRegion.cacheFileOffset;
    mappings[0].size       = subCache._readExecuteRegion.sizeInUse;
    mappings[0].maxProt    = firstMappingProt;
    mappings[0].initProt   = firstMappingProt;
    for (uint32_t i = 0; i != subCache._dataRegions.size(); ++i) {
        if ( i == 0 ) {
            assert(subCache._dataRegions[i].cacheFileOffset == subCache._readExecuteRegion.sizeInUse);
        }

        assert(subCache._dataRegions[i].initProt != 0);
        assert(subCache._dataRegions[i].maxProt != 0);

        mappings[i + 1].address    = subCache._dataRegions[i].unslidLoadAddress;
        mappings[i + 1].fileOffset = subCache._dataRegions[i].cacheFileOffset;
        mappings[i + 1].size       = subCache._dataRegions[i].sizeInUse;
        mappings[i + 1].maxProt    = subCache._dataRegions[i].maxProt;
        mappings[i + 1].initProt   = subCache._dataRegions[i].initProt;
    }

    if ( subCache._readOnlyRegion.has_value() ) {
        uint64_t previousFileOffset = subCache._readExecuteRegion.cacheFileOffset + subCache._readExecuteRegion.sizeInUse;
        if ( !subCache._dataRegions.empty() )
            previousFileOffset = subCache._dataRegions.back().cacheFileOffset + subCache._dataRegions.back().sizeInUse;
        assert(subCache._readOnlyRegion->cacheFileOffset == previousFileOffset);

        mappings[mappingCount - 1].address    = subCache._readOnlyRegion->unslidLoadAddress;
        mappings[mappingCount - 1].fileOffset = subCache._readOnlyRegion->cacheFileOffset;
        mappings[mappingCount - 1].size       = subCache._readOnlyRegion->sizeInUse;
        mappings[mappingCount - 1].maxProt    = VM_PROT_READ;
        mappings[mappingCount - 1].initProt   = VM_PROT_READ;
    }

    // Add in the new mappings with also have slide info
    dyld_cache_mapping_and_slide_info* slidableMappings = (dyld_cache_mapping_and_slide_info*)(subCache._readExecuteRegion.buffer + dyldCacheHeader->mappingWithSlideOffset);
    slidableMappings[0].address             = subCache._readExecuteRegion.unslidLoadAddress;
    slidableMappings[0].fileOffset          = subCache._readExecuteRegion.cacheFileOffset;
    slidableMappings[0].size                = subCache._readExecuteRegion.sizeInUse;
    slidableMappings[0].maxProt             = firstMappingProt;
    slidableMappings[0].initProt            = firstMappingProt;
    slidableMappings[0].slideInfoFileOffset = 0;
    slidableMappings[0].slideInfoFileSize   = 0;
    slidableMappings[0].flags               = 0;
    for (uint32_t i = 0; i != subCache._dataRegions.size(); ++i) {
        // Work out which flags this mapping has
        uint64_t flags = 0;
        if ( startsWith(subCache._dataRegions[i].name, "__AUTH") )
            flags |= DYLD_CACHE_MAPPING_AUTH_DATA;
        if ( (subCache._dataRegions[i].name == "__AUTH_DIRTY") || (subCache._dataRegions[i].name == "__DATA_DIRTY") ) {
            flags |= DYLD_CACHE_MAPPING_DIRTY_DATA;
        } else if ( (subCache._dataRegions[i].name == "__AUTH_CONST") || (subCache._dataRegions[i].name == "__DATA_CONST") ) {
            flags |= DYLD_CACHE_MAPPING_CONST_DATA;
        }

        assert(subCache._dataRegions[i].initProt != 0);
        assert(subCache._dataRegions[i].maxProt != 0);

        slidableMappings[i + 1].address             = subCache._dataRegions[i].unslidLoadAddress;
        slidableMappings[i + 1].fileOffset          = subCache._dataRegions[i].cacheFileOffset;
        slidableMappings[i + 1].size                = subCache._dataRegions[i].sizeInUse;
        slidableMappings[i + 1].maxProt             = subCache._dataRegions[i].maxProt;
        slidableMappings[i + 1].initProt            = subCache._dataRegions[i].initProt;
        slidableMappings[i + 1].slideInfoFileOffset = subCache._dataRegions[i].slideInfoFileOffset;
        slidableMappings[i + 1].slideInfoFileSize   = subCache._dataRegions[i].slideInfoFileSize;
        slidableMappings[i + 1].flags               = flags;
    }

    if ( subCache._readOnlyRegion.has_value() ) {
        slidableMappings[mappingCount - 1].address             = subCache._readOnlyRegion->unslidLoadAddress;
        slidableMappings[mappingCount - 1].fileOffset          = subCache._readOnlyRegion->cacheFileOffset;
        slidableMappings[mappingCount - 1].size                = subCache._readOnlyRegion->sizeInUse;
        slidableMappings[mappingCount - 1].maxProt             = VM_PROT_READ;
        slidableMappings[mappingCount - 1].initProt            = VM_PROT_READ;
        slidableMappings[mappingCount - 1].slideInfoFileOffset = 0;
        slidableMappings[mappingCount - 1].slideInfoFileSize   = 0;
        slidableMappings[mappingCount - 1].flags               = 0;
    }
}

void SharedCacheBuilder::writeCacheHeader()
{
    // look for libdyld.dylib and record OS verson info into cache header
    __block uint32_t osVersion    = 0;
    __block uint32_t altPlatform  = 0;
    __block uint32_t altOsVersion = 0;
    for (const DylibInfo& dylib : _sortedDylibs) {
        const char* installName = dylib.input->mappedFile.mh->installName();
        size_t nameLen = strlen(installName);
        if ( strcmp(&installName[nameLen-14], "/libdyld.dylib") == 0 ) {
            dylib.input->mappedFile.mh->forEachSupportedPlatform(^(dyld3::Platform platform, uint32_t minOS, uint32_t sdk) {
                if ( platform == _options.platform ) {
                    osVersion = minOS;
                }
                else {
                    altPlatform  = (uint32_t)platform;
                    altOsVersion = minOS;
                }
            });
            break;
        }
   }

    // Each subCache has a header to describe its layout.  Most other fields are 0
    for (const SubCache& subCache : _subCaches) {
        // The symbols cache file is shared between dev/customer, so don't give it a cacheType
        uint64_t cacheType = _options.optimizeStubs ? kDyldSharedCacheTypeProduction : kDyldSharedCacheTypeDevelopment;
        writeSharedCacheHeader(subCache, _options, *_archLayout, osVersion, altPlatform, altOsVersion, cacheType);
    }

    for (const SubCache& subCache : _subCaches) {
        dyld_cache_header* dyldSubCacheHeader   = (dyld_cache_header*)subCache._readExecuteRegion.buffer;
        dyldSubCacheHeader->imagesOffset        = (uint32_t)(dyldSubCacheHeader->mappingWithSlideOffset + dyldSubCacheHeader->mappingWithSlideCount*sizeof(dyld_cache_mapping_and_slide_info));
        dyldSubCacheHeader->imagesCount         = (uint32_t)_sortedDylibs.size() + _aliasCount;
        dyldSubCacheHeader->imagesTextOffset    = dyldSubCacheHeader->imagesOffset + sizeof(dyld_cache_image_info)*dyldSubCacheHeader->imagesCount;
        dyldSubCacheHeader->imagesTextCount     = _sortedDylibs.size();
        dyldSubCacheHeader->subCacheArrayOffset  = (uint32_t)(dyldSubCacheHeader->imagesTextOffset + sizeof(dyld_cache_image_text_info) * _sortedDylibs.size());
    }

    const SubCache& mainSubCache = _subCaches.front();
    dyld_cache_header* dyldCacheHeader = (dyld_cache_header*)mainSubCache._readExecuteRegion.buffer;
    {
        // The first subCache has an array of UUIDs for all other subCaches
        dyldCacheHeader->subCacheArrayCount   = (uint32_t)_subCaches.size() - 1;

        // The first subCache knows the size of buffer to allocate to contain all other subCaches
        // Note, we'll update this later once we have the exact size we need for these caches
        dyldCacheHeader->sharedRegionSize     = _archLayout->sharedMemorySize;
    }

    // The main cache has offsets to all the caches
    if ( _subCaches.size() > 1 ) {
        DyldSharedCache* dyldCache = (DyldSharedCache*)mainSubCache._readExecuteRegion.buffer;
        dyld_subcache_entry* subCacheEntries = (dyld_subcache_entry*)((uint8_t*)dyldCache + dyldCache->header.subCacheArrayOffset);
        for (uint64_t i = 1, e = _subCaches.size(); i != e; ++i) {
            const DyldSharedCache* subCache = (DyldSharedCache*)_subCaches[i]._readExecuteRegion.buffer;
            subCacheEntries[i - 1].cacheVMOffset = (subCache->unslidLoadAddress() - dyldCache->unslidLoadAddress());
        }
    }

    // append aliases image records and strings
/*
    for (auto &dylib : _dylibs) {
        if (!dylib->installNameAliases.empty()) {
            for (const std::string& alias : dylib->installNameAliases) {
                images->set_address(_segmentMap[dylib][0].address);
                if (_manifest.platform() == "osx") {
                    images->modTime = dylib->lastModTime;
                    images->inode = dylib->inode;
                }
                else {
                    images->modTime = 0;
                    images->inode = pathHash(alias.c_str());
                }
                images->pathFileOffset = offset;
                //fprintf(stderr, "adding alias %s for %s\n", alias.c_str(), dylib->installName.c_str());
                ::strcpy((char*)&_buffer[offset], alias.c_str());
                offset += alias.size() + 1;
                ++images;
            }
        }
    }
*/
    for (const SubCache& subCache : _subCaches) {
        dyld_cache_header* dyldSubCacheHeader   = (dyld_cache_header*)subCache._readExecuteRegion.buffer;
        // calculate start of text image array and trailing string pool
        dyld_cache_image_text_info* textImages = (dyld_cache_image_text_info*)(subCache._readExecuteRegion.buffer + dyldSubCacheHeader->imagesTextOffset);
        uint32_t stringOffset = (uint32_t)(dyldSubCacheHeader->subCacheArrayOffset + sizeof(dyld_subcache_entry) * dyldSubCacheHeader->subCacheArrayCount);

        // write text image array and image names pool at same time
        for (const DylibInfo& dylib : _sortedDylibs) {
            dylib.input->mappedFile.mh->getUuid(textImages->uuid);
            textImages->loadAddress     = dylib.cacheLocation[0].dstCacheUnslidAddress;
            textImages->textSegmentSize = (uint32_t)dylib.cacheLocation[0].dstCacheSegmentSize;
            textImages->pathOffset      = stringOffset;
            const char* installName = dylib.input->mappedFile.mh->installName();
            ::strcpy((char*)subCache._readExecuteRegion.buffer + stringOffset, installName);
            stringOffset += (uint32_t)strlen(installName)+1;
            ++textImages;
        }

        // fill in image table.  This has to be after the above loop so that the install names are within 32-bits of the first shared cache
        // Note, this code is paired with an adjustment in addObjcSegments
        textImages = (dyld_cache_image_text_info*)(subCache._readExecuteRegion.buffer + dyldSubCacheHeader->imagesTextOffset);
        dyld_cache_image_info* images = (dyld_cache_image_info*)(subCache._readExecuteRegion.buffer + dyldSubCacheHeader->imagesOffset);
        for (const DylibInfo& dylib : _sortedDylibs) {
            images->address = dylib.cacheLocation[0].dstCacheUnslidAddress;
            if ( _options.dylibsRemovedDuringMastering ) {
                images->modTime = 0;
                images->inode   = 0;
            }
            else {
                images->modTime = dylib.input->mappedFile.modTime;
                images->inode   = dylib.input->mappedFile.inode;
            }
            images->pathFileOffset = (uint32_t)textImages->pathOffset;
            ++images;
            ++textImages;
        }

        // make sure header did not overflow into first mapped image
        const dyld_cache_image_info* firstImage = (dyld_cache_image_info*)(subCache._readExecuteRegion.buffer + dyldSubCacheHeader->imagesOffset);
        assert(stringOffset <= (firstImage->address - subCache._readExecuteRegion.unslidLoadAddress));
    }
}

void SharedCacheBuilder::processSelectorStrings(const std::vector<LoadedMachO>& executables, IMPCaches::HoleMap& selectorsHoleMap) {
    const bool log = false;

    // We only do this optimisation to reduce the size of the shared cache executable closures
    // Skip this is those closures are not being built
    if ( !_options.optimizeDyldDlopens || !_options.optimizeDyldLaunches )
        return;

    _selectorStringsFromExecutables = 0;
    uint64_t totalBytesPulledIn = 0;

    // Don't do this optimisation on watchOS where the shared cache is too small
    if (_options.platform == dyld3::Platform::watchOS)
        return;

    // Get the method name coalesced section as that is where we need to put these strings
    CacheBuilder::CacheCoalescedText::StringSection& cacheStringSection = _coalescedText.getSectionData("__objc_methname");
    for (const LoadedMachO& executable : executables) {
        const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)executable.loadedFileInfo.fileContent;

        uint64_t sizeBeforeProcessing = cacheStringSection.bufferSize;

        ma->forEachObjCMethodName(^(const char* methodName) {
            std::string_view str = methodName;
            if (cacheStringSection.stringsToOffsets.find(str) == cacheStringSection.stringsToOffsets.end()) {
                int offset = selectorsHoleMap.addStringOfSize((unsigned)str.size() + 1);
                cacheStringSection.stringsToOffsets[str] = offset;

                // If we inserted the string past the end then we need to include it in the total
                int possibleNewEnd = offset + (int)str.size() + 1;
                if (cacheStringSection.bufferSize < (uint32_t)possibleNewEnd) {
                    cacheStringSection.bufferSize = (uint32_t)possibleNewEnd;
                }
                // if (log) printf("Selector: %s -> %s\n", ma->installName(), methodName);
                ++_selectorStringsFromExecutables;
            }
        });

        uint64_t sizeAfterProcessing = cacheStringSection.bufferSize;
        totalBytesPulledIn += (sizeAfterProcessing - sizeBeforeProcessing);
        if ( log && (sizeBeforeProcessing != sizeAfterProcessing) ) {
            printf("Pulled in % 6lld bytes of selectors from %s\n",
                   sizeAfterProcessing - sizeBeforeProcessing, executable.loadedFileInfo.path);
        }
    }

    _diagnostics.verbose("Pulled in %lld selector strings (%lld bytes) from executables\n",
                         _selectorStringsFromExecutables, totalBytesPulledIn);
}

static void visitSelectorString(CacheBuilder::CacheCoalescedText::StringSection& cacheStringSection,
                                const IMPCaches::SelectorMap& selectors,
                                IMPCaches::HoleMap& selectorsHoleMap,
                                std::string_view str) {
    if (cacheStringSection.stringsToOffsets.find(str) == cacheStringSection.stringsToOffsets.end()) {
        const IMPCaches::SelectorMap::UnderlyingMap & map = selectors.map;
        IMPCaches::SelectorMap::UnderlyingMap::const_iterator selectorsIterator = map.find(str);

        int cacheSectionOffset = 0;
        if (selectorsIterator != map.end()) {
            cacheSectionOffset = selectorsIterator->second->offset;
        } else {
            cacheSectionOffset = selectorsHoleMap.addStringOfSize((unsigned)str.size() + 1);
        }

        cacheStringSection.stringsToOffsets[str] = cacheSectionOffset;
        uint32_t sizeAtLeast = cacheSectionOffset + (uint32_t)str.size() + 1;
        if (cacheStringSection.bufferSize < sizeAtLeast) {
            cacheStringSection.bufferSize = sizeAtLeast;
        }
        // if (log) printf("Selector: %s -> %s\n", ma->installName(), methodName);
    }
}

static void processSelectorStrings(Diagnostics& diags,
                                   CacheBuilder::CacheCoalescedText::StringSection& cacheStringSection,
                                   const IMPCaches::SelectorMap& selectors,
                                   IMPCaches::HoleMap& selectorsHoleMap,
                                   const dyld3::MachOAnalyzer* ma)
{
    intptr_t slide = ma->getSlide();
    uint32_t pointerSize = ma->pointerSize();
    auto vmAddrConverter = ma->makeVMAddrConverter(false);

    auto visitMethodName = ^(const char* methodName) {
        visitSelectorString(cacheStringSection, selectors, selectorsHoleMap, methodName);
    };

    ma->forEachObjCMethodName(visitMethodName);

    auto visitReferenceToObjCSelector = ^void(uint64_t selectorStringVMAddr, uint64_t selectorReferenceVMAddr) {
        const char* selectorString = (const char*)(selectorStringVMAddr + slide);
        visitMethodName(selectorString);
    };

    auto visitMethod = ^(uint64_t methodVMAddr, const dyld3::MachOAnalyzer::ObjCMethod& method,
                         bool& stopMethod) {
        visitReferenceToObjCSelector(method.nameVMAddr, method.nameLocationVMAddr);
    };

    auto visitMethodList = ^(uint64_t methodListVMAddr) {
        if ( methodListVMAddr == 0 )
            return;
        ma->forEachObjCMethod(methodListVMAddr, vmAddrConverter, 0, visitMethod);
        if (diags.hasError())
            return;
    };

    auto visitClass = ^(uint64_t classVMAddr,
                        uint64_t classSuperclassVMAddr, uint64_t classDataVMAddr,
                        const dyld3::MachOAnalyzer::ObjCClassInfo& objcClass, bool isMetaClass,
                        bool& stopClass) {
        visitMethodList(objcClass.baseMethodsVMAddr(pointerSize));
    };

    auto visitCategory = ^(uint64_t categoryVMAddr,
                           const dyld3::MachOAnalyzer::ObjCCategory& objcCategory,
                           bool& stopCategory) {
        visitMethodList(objcCategory.instanceMethodsVMAddr);
        visitMethodList(objcCategory.classMethodsVMAddr);
    };
    auto visitProtocol = ^(uint64_t protocolVMAddr,
                           const dyld3::MachOAnalyzer::ObjCProtocol& objCProtocol,
                           bool& stopProtocol) {
        visitMethodList(objCProtocol.instanceMethodsVMAddr);
        visitMethodList(objCProtocol.classMethodsVMAddr);
        visitMethodList(objCProtocol.optionalInstanceMethodsVMAddr);
        visitMethodList(objCProtocol.optionalClassMethodsVMAddr);
    };

    // Walk the class list
    ma->forEachObjCClass(diags, vmAddrConverter, visitClass);
    if (diags.hasError())
        return;

    // Walk the category list
    ma->forEachObjCCategory(diags, vmAddrConverter, visitCategory);
    if (diags.hasError())
        return;

    // Walk the protocol list
    ma->forEachObjCProtocol(diags, vmAddrConverter, visitProtocol);
    if (diags.hasError())
        return;

    // Visit the selector refs
    ma->forEachObjCSelectorReference(diags, vmAddrConverter, ^(uint64_t selRefVMAddr, uint64_t selRefTargetVMAddr, bool& stopSelRef) {
        visitReferenceToObjCSelector(selRefTargetVMAddr, selRefVMAddr);
    });
    if (diags.hasError())
        return;
}

static void visitClassNameString(CacheBuilder::CacheCoalescedText::StringSection& cacheStringSection,
                                 std::string_view str) {
    if (cacheStringSection.stringsToOffsets.find(str) == cacheStringSection.stringsToOffsets.end()) {
        auto itAndInserted = cacheStringSection.stringsToOffsets.insert({ str, cacheStringSection.bufferSize });
        assert(itAndInserted.second);

        cacheStringSection.bufferSize += str.size() + 1;
    }
}

static void processClassNameStrings(Diagnostics& diags,
                                    CacheBuilder::CacheCoalescedText::StringSection& cacheStringSection,
                                    const dyld3::MachOAnalyzer* ma)
{
    intptr_t slide = ma->getSlide();
    uint32_t pointerSize = ma->pointerSize();
    auto vmAddrConverter = ma->makeVMAddrConverter(false);

    auto visitClass = ^(uint64_t classVMAddr,
                        uint64_t classSuperclassVMAddr, uint64_t classDataVMAddr,
                        const dyld3::MachOAnalyzer::ObjCClassInfo& objcClass, bool isMetaClass,
                        bool& stopClass) {
        uint64_t classNameVMAddr = objcClass.nameVMAddr(pointerSize);
        const char* name = (const char*)(classNameVMAddr + slide);
        visitClassNameString(cacheStringSection, name);
    };

    auto visitCategory = ^(uint64_t categoryVMAddr,
                           const dyld3::MachOAnalyzer::ObjCCategory& objcCategory,
                           bool& stopCategory) {
        const char* name = (const char*)(objcCategory.nameVMAddr + slide);
        visitClassNameString(cacheStringSection, name);
    };
    auto visitProtocol = ^(uint64_t protocolVMAddr,
                           const dyld3::MachOAnalyzer::ObjCProtocol& objCProtocol,
                           bool& stopProtocol) {
        const char* name = (const char*)(objCProtocol.nameVMAddr + slide);
        visitClassNameString(cacheStringSection, name);
    };

    // Walk the class list
    ma->forEachObjCClass(diags, vmAddrConverter, visitClass);
    if (diags.hasError())
        return;

    // Walk the category list
    ma->forEachObjCCategory(diags, vmAddrConverter, visitCategory);
    if (diags.hasError())
        return;

    // Walk the protocol list
    ma->forEachObjCProtocol(diags, vmAddrConverter, visitProtocol);
    if (diags.hasError())
        return;
}

void SharedCacheBuilder::parseCoalescableSegments(IMPCaches::SelectorMap& selectors, IMPCaches::HoleMap& selectorsHoleMap) {
    const bool log = false;

    // Don't do this on driverKit.  The strings would go in to the liobjc __OBJC_RO segment, but there's no objc there
    if ( _options.platform == dyld3::Platform::driverKit )
        return;

    // Always add the magic selector first.  It doesn't hurt to have it even if the arch we are on doesn't use it
    {
        // Get the method name coalesced section as that is where we need to put these strings
        CacheBuilder::CacheCoalescedText::StringSection& cacheStringSection = _coalescedText.getSectionData("__objc_methname");

        // Process the magic selector first, so that we know its the base of all other strings
        // This is used later for relative method lists
        constexpr std::string_view magicSelector = "\xf0\x9f\xa4\xaf";
        visitSelectorString(cacheStringSection, selectors, selectorsHoleMap, magicSelector.data());
    }


    if ( _archLayout->subCacheTextLimit != 0 ) {
        // Sub caches don't support coalesced strings for all sections.  However, we have to copy the selector strings in libobjc
        // so that shared cache relative method lists can be offsets from libobjc's mach header

        // Get the method name coalesced section as that is where we need to put these strings
        CacheBuilder::CacheCoalescedText::StringSection& cacheMethodNameSection = _coalescedText.getSectionData("__objc_methname");

        for (DylibInfo& dylib : _sortedDylibs) {
            ::processSelectorStrings(_diagnostics, cacheMethodNameSection, selectors, selectorsHoleMap, dylib.input->mappedFile.mh);
            if ( _diagnostics.hasError() )
                return;
        }

        // Class/protocol names are also offsets.  We need to copy them too
        CacheBuilder::CacheCoalescedText::StringSection& cacheClassNameSection = _coalescedText.getSectionData("__objc_classname");

        for (DylibInfo& dylib : _sortedDylibs) {
            ::processClassNameStrings(_diagnostics, cacheClassNameSection, dylib.input->mappedFile.mh);
            if ( _diagnostics.hasError() )
                return;
        }
    }

    // FIXME: Coalesce strings within each subcache.
    // For now, we only support coalescing strings if the cache size is <= 2GB.  Otherwise we don't know
    // if a signed 32-bit offset somewhere will overflow
    if ( _archLayout->subCacheTextLimit != 0 ) {
        if ( _archLayout->sharedMemorySize > 0x80000000ULL )
            return;
    }

    for (DylibInfo& dylib : _sortedDylibs)
        _coalescedText.parseCoalescableText(dylib.input->mappedFile.mh, dylib.textCoalescer, selectors, selectorsHoleMap);

    if (log) {
        for (const char* section : CacheCoalescedText::SupportedSections) {
            CacheCoalescedText::StringSection& sectionData = _coalescedText.getSectionData(section);
            printf("Coalesced %s from % 10lld -> % 10d, saving % 10lld bytes\n", section,
                   sectionData.bufferSize + sectionData.savedSpace, sectionData.bufferSize, sectionData.savedSpace);
        }
    }

    // arm64e needs to convert CF constants to tagged pointers
    if ( strcmp(_archLayout->archName, "arm64e") == 0 ) {
        // Find the dylib which exports the CFString ISA.  It's likely CoreFoundation but it could move
        CacheCoalescedText::CFSection& cfStrings = _coalescedText.cfStrings;
        for (DylibInfo& dylib : _sortedDylibs) {
            const dyld3::MachOAnalyzer* ma = dylib.input->mappedFile.mh;
            dyld3::MachOAnalyzer::FoundSymbol foundInfo;
            bool foundISASymbol = ma->findExportedSymbol(_diagnostics, cfStrings.isaClassName, false, foundInfo, nullptr);
            if ( foundISASymbol ) {
                // This dylib exports the ISA, so everyone else should look here for the ISA too.
                if ( cfStrings.isaInstallName != nullptr ) {
                    // Found a duplicate.  We can't do anything here
                    _diagnostics.verbose("Could not optimize CFString's due to duplicate ISA symbols");
                    cfStrings.isaInstallName = nullptr;
                    break;
                } else {
                    cfStrings.isaInstallName = ma->installName();
                    cfStrings.isaVMOffset    = foundInfo.value;
                }
            }
        }
        if ( cfStrings.isaInstallName != nullptr ) {
            for (DylibInfo& dylib : _sortedDylibs) {
                _coalescedText.parseCFConstants(dylib.input->mappedFile.mh, dylib.textCoalescer);
            }
        }
    }
}

// Works out how many sub caches we need.  And partitions dylibs in to them
void SharedCacheBuilder::computeSubCaches()
{

    uint64_t objcROSize = 0;
    uint64_t objcRWSize = 0;
    {
        // Calculate how much space we need for objc
        uint32_t totalSelectorRefCount = (uint32_t)_selectorStringsFromExecutables;
        uint32_t totalClassDefCount    = 0;
        uint32_t totalProtocolDefCount = 0;
        for (DylibInfo& dylib : _sortedDylibs) {
            dyld3::MachOAnalyzer::ObjCInfo info = dylib.input->mappedFile.mh->getObjCInfo();
            totalSelectorRefCount   += info.selRefCount;
            totalClassDefCount      += info.classDefCount;
            totalProtocolDefCount   += info.protocolDefCount;
        }

        // now that shared cache coalesces all selector strings, use that better count
        uint32_t coalescedSelectorCount = (uint32_t)_coalescedText.objcMethNames.stringsToOffsets.size();
        if ( coalescedSelectorCount > totalSelectorRefCount )
            totalSelectorRefCount = coalescedSelectorCount;

        objcROSize = align(computeReadOnlyObjC(totalSelectorRefCount, totalClassDefCount, totalProtocolDefCount), 14);
        objcRWSize = align(computeReadWriteObjC((uint32_t)_sortedDylibs.size(), totalProtocolDefCount), 14);
    }

    // Calculate how much space we need for Swift
    uint64_t swiftROSize = computeReadOnlySwift();

    // Coalsced strings also go in _OBJC_RO
    uint64_t coalescedStringsSize = 0;
    for (const char* section: CacheCoalescedText::SupportedSections) {
        CacheCoalescedText::StringSection& cacheStringSection = _coalescedText.getSectionData(section);
        coalescedStringsSize += cacheStringSection.bufferSize;
    }

    const size_t dylibCount = _sortedDylibs.size();
    if ( _archLayout->subCacheTextLimit != 0 ) {
        // FIXME: This only counts __TEXT, on the assumption that __TEXT is always the largest segment in the dylibs
        __block uint64_t currentVMSize = 0;
        uint64_t firstIndex = 0;
        for (uint64_t dylibIndex = 0; dylibIndex < dylibCount; ++dylibIndex) {
            const DylibInfo& dylib = _sortedDylibs[dylibIndex];
            dylib.input->mappedFile.mh->forEachSegment(^(const dyld3::MachOFile::SegmentInfo& segInfo, bool& stop) {
                if ( strcmp(segInfo.segName, "__TEXT") == 0 ) {
                    // FIXME: Take in to account space saved from coalesced string sections
                    currentVMSize += segInfo.vmSize;
                    stop = true;
                }
            });

            // __OBJC_RO gets all the hash tables and coalesced strings
            if ( !strcmp(dylib.input->mappedFile.mh->installName(), "/usr/lib/libobjc.A.dylib") ) {
                currentVMSize += objcROSize + swiftROSize + coalescedStringsSize;
            }

            const uint64_t regionLimit = _archLayout->subCacheTextLimit;
            if ( currentVMSize > regionLimit ) {
                // Make a cache for all dylibs up to (but not including this one) as this one
                // pushed us over the limit
                SubCache& subCache = _subCaches.emplace_back();
                subCache._textFirstDylibIndex = firstIndex;
                subCache._textNumDylibs = dylibIndex - firstIndex;

                // Move firstIndex to take account of this subCache we've computed
                firstIndex = dylibIndex;
                currentVMSize = 0;
            }
        }

        // There will always be a left over cache to add.  Either it will be the only cache, if the size
        // never passed the limit, or we have the last few dylibs to add to a cache
        SubCache& subCache = _subCaches.emplace_back();
        subCache._textFirstDylibIndex = firstIndex;
        subCache._textNumDylibs = dylibCount - firstIndex;
    } else {
        // Just add a single subCache for everything
        SubCache& subCache = _subCaches.emplace_back();
        subCache._textFirstDylibIndex = 0;
        subCache._textNumDylibs = dylibCount;
    }

    // Some archs can't handle DATA and LINKEDIT in each file.  Instead they need to put it in the last file so that
    // unoptimized LINKEDIT doesn't consume too much VM space.
    if ( _archLayout->useSplitCacheLayout ) {
        // DATA and LINKEDIT are only in the last subCaches, and so contain those segments for all dylibs
        // We don't want to append DATA and LINKEDIT to the last subCache TEXT.  Instead create new subCaches

        // DATA
        SubCache& dataSubCache = _subCaches.emplace_back();
        dataSubCache._textFirstDylibIndex = 0;
        dataSubCache._textNumDylibs = 0;
        dataSubCache._dataFirstDylibIndex = 0;
        dataSubCache._dataNumDylibs = dylibCount;
        dataSubCache._linkeditFirstDylibIndex = 0;
        dataSubCache._linkeditNumDylibs = 0;

        SubCache& linkeditSubCache = _subCaches.emplace_back();
        linkeditSubCache._textFirstDylibIndex = 0;
        linkeditSubCache._textNumDylibs = 0;
        linkeditSubCache._dataFirstDylibIndex = 0;
        linkeditSubCache._dataNumDylibs = 0;
        linkeditSubCache._linkeditFirstDylibIndex = 0;
        linkeditSubCache._linkeditNumDylibs = dylibCount;
        // The end of the DATA subcache ended with LINKEDIT.  The LINKEDIT cache starts with TEXT then more LINKEDIT
        // We don't need any padding in this last subcache, between any of those LINKEDIT -> TEXT -> LINKEDIT regions
        linkeditSubCache._addPaddingAfterText = false;
        linkeditSubCache._addPaddingAfterData = false;
    } else {
        // If the total cache size is under 4GB, then we can still use 32-bit LINKEDIT offsets.  Then we should just use
        // a single LINKEDIT, not one per sub cache
        if ( _archLayout->sharedMemorySize <= 0x100000000ULL ) {
            // Each __DATA, __LINKEDIT just goes in the same cache and that dylib's text, so copy over the ranges
            for (SubCache& subCache : _subCaches) {
                subCache._dataFirstDylibIndex = subCache._textFirstDylibIndex;
                subCache._dataNumDylibs = subCache._textNumDylibs;
                subCache._linkeditFirstDylibIndex = 0;
                subCache._linkeditNumDylibs = 0;
            }
            SubCache& lastSubCache = _subCaches.back();
            lastSubCache._linkeditFirstDylibIndex = 0;
            lastSubCache._linkeditNumDylibs = dylibCount;
        } else {
            // Each __DATA, __LINKEDIT just goes in the same cache and that dylib's text, so copy over the ranges
            for (SubCache& subCache : _subCaches) {
                subCache._dataFirstDylibIndex = subCache._textFirstDylibIndex;
                subCache._dataNumDylibs = subCache._textNumDylibs;
                subCache._linkeditFirstDylibIndex = subCache._textFirstDylibIndex;
                subCache._linkeditNumDylibs = subCache._textNumDylibs;
            }
        }
    }

    // Assign ASLRTracker's to all dylibs
    for (SubCache& subCache : _subCaches) {
        // Skip subCache's which don't contain DATA
        if ( subCache._dataNumDylibs == 0 )
            continue;

        dyld3::Array<DylibInfo> cacheImages(_sortedDylibs.data(), _sortedDylibs.size(), _sortedDylibs.size());
        dyld3::Array<DylibInfo> subCacheImages = cacheImages.subArray(subCache._dataFirstDylibIndex, subCache._dataNumDylibs);

        for (DylibInfo& dylibInfo : subCacheImages)
            dylibInfo._aslrTracker = &subCache._aslrTracker;
    }

    // Work out which subCache contains RO/RW for libdyld and libobjc
    for (SubCache& subCache : _subCaches) {
        dyld3::Array<DylibInfo> cacheImages(_sortedDylibs.data(), _sortedDylibs.size(), _sortedDylibs.size());

        dyld3::Array<DylibInfo> subCacheTextImages = cacheImages.subArray(subCache._textFirstDylibIndex, subCache._textNumDylibs);
        for (DylibInfo& dylib : subCacheTextImages) {
            if ( !strcmp(dylib.input->mappedFile.mh->installName(), "/usr/lib/libobjc.A.dylib") ) {
                _objcReadOnlyMetadataSubCache = &subCache;
                break;
            }
        }

        // Skip subCache's which don't contain DATA
        if ( subCache._dataNumDylibs == 0 )
            continue;
        dyld3::Array<DylibInfo> subCacheDataImages = cacheImages.subArray(subCache._dataFirstDylibIndex, subCache._dataNumDylibs);
        for (DylibInfo& dylib : subCacheDataImages) {
            if ( !strcmp(dylib.input->mappedFile.mh->installName(), "/usr/lib/libobjc.A.dylib") ) {
                _objcReadWriteMetadataSubCache = &subCache;
            }
        }
    }

    assignSegmentAddresses(objcROSize, objcRWSize, swiftROSize);
}

void SharedCacheBuilder::assignReadExecuteSegmentAddresses(SubCache& subCache, uint64_t& addr, uint64_t& cacheFileOffset,
                                                           size_t startOffset, uint64_t objcROSize, uint64_t swiftROSize)
{
    dyld3::Array<DylibInfo> cacheImages(_sortedDylibs.data(), _sortedDylibs.size(), _sortedDylibs.size());
    dyld3::Array<DylibInfo> subCacheImages = cacheImages.subArray(subCache._textFirstDylibIndex, subCache._textNumDylibs);

    bool isObjCSubCache = (&subCache == _objcReadOnlyMetadataSubCache);

    // assign TEXT segment addresses
    subCache._readExecuteRegion.buffer               = (uint8_t*)_fullAllocatedBuffer + addr - _archLayout->sharedMemoryStart;
    subCache._readExecuteRegion.bufferSize           = 0;
    subCache._readExecuteRegion.sizeInUse            = 0;
    subCache._readExecuteRegion.unslidLoadAddress    = addr;
    subCache._readExecuteRegion.cacheFileOffset      = cacheFileOffset;

    addr +=  startOffset; // header

    for (DylibInfo& dylib : subCacheImages) {
        __block uint64_t textSegVmAddr = 0;
        dylib.input->mappedFile.mh->forEachSegment(^(const dyld3::MachOFile::SegmentInfo& segInfo, bool& stop) {
            if ( strcmp(segInfo.segName, "__TEXT") == 0 )
                textSegVmAddr = segInfo.vmAddr;
            if ( segInfo.protections != (VM_PROT_READ | VM_PROT_EXECUTE) )
                return;
            // We may have coalesced the sections at the end of this segment.  In that case, shrink the segment to remove them.
            __block size_t sizeOfSections = 0;
            __block bool foundCoalescedSection = false;
            dylib.input->mappedFile.mh->forEachSection(^(const dyld3::MachOAnalyzer::SectionInfo &sectInfo, bool malformedSectionRange, bool &stopSection) {
                if (strcmp(sectInfo.segInfo.segName, segInfo.segName) != 0)
                    return;
                if ( dylib.textCoalescer.sectionWasCoalesced(segInfo.segName, sectInfo.sectName)) {
                    foundCoalescedSection = true;
                } else {
                    sizeOfSections = sectInfo.sectAddr + sectInfo.sectSize - segInfo.vmAddr;
                }
            });
            if (!foundCoalescedSection)
                sizeOfSections = segInfo.sizeOfSections;

            // Keep __TEXT segments 4K or more aligned
            addr = align(addr, std::max((int)segInfo.p2align, (int)12));
            uint64_t offsetInRegion = addr - subCache._readExecuteRegion.unslidLoadAddress;
            SegmentMappingInfo loc;
            loc.srcSegment             = (uint8_t*)dylib.input->mappedFile.mh + segInfo.vmAddr - textSegVmAddr;
            loc.segName                = segInfo.segName;
            loc.dstSegment             = subCache._readExecuteRegion.buffer + offsetInRegion;
            loc.dstCacheUnslidAddress  = addr;
            loc.dstCacheFileOffset     = (uint32_t)offsetInRegion;
            loc.dstCacheSegmentSize    = (uint32_t)align(sizeOfSections, 12);
            loc.dstCacheFileSize       = (uint32_t)align(sizeOfSections, 12);
            loc.copySegmentSize        = (uint32_t)sizeOfSections;
            loc.srcSegmentIndex        = segInfo.segIndex;
            dylib.cacheLocation.push_back(loc);
            addr += loc.dstCacheSegmentSize;
        });
    }

    if ( isObjCSubCache ) {
        assignObjCROAddress(subCache, addr, objcROSize);
        // HACK: Put Swift in the same file as objc for now.  This can actually go in any file
        // so this one is as good as any other
        assignSwiftROAddress(subCache, addr, swiftROSize);
    }

    addr = align(addr, _archLayout->sharedRegionAlignP2);

    // align TEXT region end
    uint64_t endTextAddress = addr;
    subCache._readExecuteRegion.bufferSize = endTextAddress - subCache._readExecuteRegion.unslidLoadAddress;
    subCache._readExecuteRegion.sizeInUse  = subCache._readExecuteRegion.bufferSize;

    cacheFileOffset += subCache._readExecuteRegion.sizeInUse;
}

void SharedCacheBuilder::assignObjCROAddress(SubCache& subCache, uint64_t& addr, uint64_t objcROSize)
{
    // reserve space for objc optimization tables and deduped strings
    uint64_t objcReadOnlyBufferVMAddr = addr;
    _objcReadOnlyBuffer = subCache._readExecuteRegion.buffer + (addr - subCache._readExecuteRegion.unslidLoadAddress);

    // First the strings as we'll fill in the objc tables later in the optimizer
    for (const char* section: CacheCoalescedText::SupportedSections) {
        CacheCoalescedText::StringSection& cacheStringSection = _coalescedText.getSectionData(section);
        cacheStringSection.bufferAddr = subCache._readExecuteRegion.buffer + (addr - subCache._readExecuteRegion.unslidLoadAddress);
        cacheStringSection.bufferVMAddr = addr;
        addr += cacheStringSection.bufferSize;
    }

    addr = align(addr, 14);
    _objcReadOnlyBufferSizeUsed = addr - objcReadOnlyBufferVMAddr;

    addr += objcROSize;

    size_t impCachesSize = _impCachesBuilder->totalIMPCachesSize();
    size_t alignedImpCachesSize = align(impCachesSize, 14);
    _diagnostics.verbose("Reserving %zd bytes for IMP caches (aligned to %zd)\n", impCachesSize, alignedImpCachesSize);
    addr += alignedImpCachesSize;

    _objcReadOnlyBufferSizeAllocated = addr - objcReadOnlyBufferVMAddr;
}

void SharedCacheBuilder::assignSwiftROAddress(SubCache &subCache, uint64_t &addr, uint64_t swiftROSize)
{
    // reserve space for Swift optimization tables
    uint64_t swiftReadOnlyBufferVMAddr = addr;
    _swiftReadOnlyBuffer = subCache._readExecuteRegion.buffer + (addr - subCache._readExecuteRegion.unslidLoadAddress);

    // Calculate how much space Swift needs
    _diagnostics.verbose("Reserving %lld bytes for read-only Swift\n", swiftROSize);
    addr += swiftROSize;

    addr = align(addr, 14);
    _swiftReadOnlyBufferSizeAllocated = addr - swiftReadOnlyBufferVMAddr;
}

// This is the new method which will put all __DATA* mappings in to a their own mappings
void SharedCacheBuilder::assignDataSegmentAddresses(SubCache& subCache,
                                                    uint64_t& addr, uint64_t& cacheFileOffset,
                                                    uint64_t objcRWSize)
{
    // Skip subCache's which don't contain DATA
    if ( subCache._dataNumDylibs == 0 )
        return;

    bool isObjCSubCache = (&subCache == _objcReadWriteMetadataSubCache);

    dyld3::Array<DylibInfo> cacheImages(_sortedDylibs.data(), _sortedDylibs.size(), _sortedDylibs.size());
    dyld3::Array<DylibInfo> subCacheImages = cacheImages.subArray(subCache._dataFirstDylibIndex, subCache._dataNumDylibs);
    __block dyld3::Array<DylibInfo>* subCacheImagesBlockPointer = &subCacheImages;

    uint64_t nextRegionFileOffset = cacheFileOffset;

    const size_t dylibCount = subCache._dataNumDylibs;
    BLOCK_ACCCESSIBLE_ARRAY(uint32_t, dirtyDataSortIndexes, dylibCount);
    for (size_t i=0; i < dylibCount; ++i)
        dirtyDataSortIndexes[i] = (uint32_t)i;
    std::sort(&dirtyDataSortIndexes[0], &dirtyDataSortIndexes[dylibCount], [&](const uint32_t& a, const uint32_t& b) {
        const auto& orderA = _options.dirtyDataSegmentOrdering.find(subCacheImages[a].input->mappedFile.runtimePath);
        const auto& orderB = _options.dirtyDataSegmentOrdering.find(subCacheImages[b].input->mappedFile.runtimePath);
        bool foundA = (orderA != _options.dirtyDataSegmentOrdering.end());
        bool foundB = (orderB != _options.dirtyDataSegmentOrdering.end());

        // Order all __DATA_DIRTY segments specified in the order file first, in the order specified in the file,
        // followed by any other __DATA_DIRTY segments in lexicographic order.
        if ( foundA && foundB )
            return orderA->second < orderB->second;
        else if ( foundA )
            return true;
        else if ( foundB )
             return false;
        else
             return subCacheImages[a].input->mappedFile.runtimePath < subCacheImages[b].input->mappedFile.runtimePath;
    });

    bool supportsAuthFixups = false;

    // This tracks which segments contain authenticated data, even if their name isn't __AUTH*
    std::set<uint32_t> authenticatedSegments[dylibCount];
    if ( strcmp(_archLayout->archName, "arm64e") == 0 ) {
        supportsAuthFixups = true;

        for (DylibInfo& dylib : subCacheImages) {
            uint64_t dylibIndex = subCacheImages.index(dylib);
            __block std::set<uint32_t>& authSegmentIndices = authenticatedSegments[dylibIndex];
            // Put all __DATA_DIRTY segments in the __AUTH region first, then we don't need to walk their chains
            dylib.input->mappedFile.mh->forEachSegment(^(const dyld3::MachOFile::SegmentInfo& segInfo, bool& stop) {
                if ( strcmp(segInfo.segName, "__DATA_DIRTY") == 0 ) {
                    authSegmentIndices.insert(segInfo.segIndex);
                    stop = true;
                }
            });
            dylib.input->mappedFile.mh->withChainStarts(_diagnostics, 0,
                                                        ^(const dyld_chained_starts_in_image *starts) {
                dylib.input->mappedFile.mh->forEachFixupChainSegment(_diagnostics, starts,
                                                                     ^(const dyld_chained_starts_in_segment* segmentInfo, uint32_t segIndex, bool& stopSegment) {
                    // Skip walking segments we already know are __AUTH, ie, __DATA_DIRTY
                    if ( authSegmentIndices.count(segIndex) )
                        return;

                    dylib.input->mappedFile.mh->forEachFixupInSegmentChains(_diagnostics, segmentInfo, false,
                                                                            ^(dyld3::MachOLoaded::ChainedFixupPointerOnDisk* fixupLoc, const dyld_chained_starts_in_segment* segInfo, bool& stopChain) {
                        uint16_t chainedFixupsFormat = segInfo->pointer_format;
                        assert( (chainedFixupsFormat == DYLD_CHAINED_PTR_ARM64E) || (chainedFixupsFormat == DYLD_CHAINED_PTR_ARM64E_USERLAND) || (chainedFixupsFormat == DYLD_CHAINED_PTR_ARM64E_USERLAND24) );

                        if ( fixupLoc->arm64e.authRebase.auth ) {
                            authSegmentIndices.insert(segIndex);
                            stopChain = true;
                            return;
                        }
                    });
                });
            });
        }
    }

    // Categorize each segment in each binary
    enum class SegmentType : uint8_t {
        skip,                       // used for non-data segments we should ignore here
        data,
        dataDirty,
        dataConst,
        dataConstWorkarounds,       // temporary.  Used for dylibs with bad __DATA_CONST
        auth,
        authDirty,
        authConst,
        authConstWorkarounds,       // temporary.  Used for dylibs with bad __AUTH_CONST
    };

    BLOCK_ACCCESSIBLE_ARRAY(uint64_t, textSegVmAddrs, dylibCount);
    BLOCK_ACCCESSIBLE_ARRAY(std::vector<SegmentType>, segmentTypes, dylibCount);

    // Just in case __AUTH is used in a non-arm64e binary, we can force it to use data enums
    SegmentType authSegment                 = supportsAuthFixups ? SegmentType::auth      : SegmentType::data;
    SegmentType authConstSegment            = supportsAuthFixups ? SegmentType::authConst : SegmentType::dataConst;
    SegmentType authConstWorkaroundSegment  = supportsAuthFixups ? SegmentType::authConstWorkarounds : SegmentType::dataConstWorkarounds;

    for (const DylibInfo& dylib : subCacheImages) {
        uint64_t dylibIndex = subCacheImages.index(dylib);
        __block std::set<uint32_t>& authSegmentIndices = authenticatedSegments[dylibIndex];
        __block std::vector<SegmentType>& dylibSegmentTypes = segmentTypes[dylibIndex];
        uint64_t &textSegVmAddr = textSegVmAddrs[dylibIndex];
        dylib.input->mappedFile.mh->forEachSegment(^(const dyld3::MachOFile::SegmentInfo& segInfo, bool& stop) {
            if ( strcmp(segInfo.segName, "__TEXT") == 0 ) {
                textSegVmAddr = segInfo.vmAddr;
            }

            // Skip non-DATA segments
            if ( segInfo.protections != (VM_PROT_READ | VM_PROT_WRITE) ) {
                dylibSegmentTypes.push_back(SegmentType::skip);
                return;
            }

            // If we don't have split seg v2, then all remaining segments must look like __DATA so that they
            // stay contiguous
            if (!dylib.input->mappedFile.mh->isSplitSegV2()) {
                dylibSegmentTypes.push_back(SegmentType::data);
                return;
            }

            __block bool supportsDataConst = true;
            if ( dylib.input->mappedFile.mh->isSwiftLibrary() ) {
                uint64_t objcConstSize = 0;
                bool containsObjCSection = dylib.input->mappedFile.mh->findSectionContent(segInfo.segName, "__objc_const", objcConstSize);

                // <rdar://problem/66284631> Don't put __objc_const read-only memory as Swift has method lists we can't see
                if ( containsObjCSection )
                    supportsDataConst = false;
            } else if ( !strcmp(dylib.input->mappedFile.mh->installName(), "/System/Library/Frameworks/Foundation.framework/Foundation") ||
                        !strcmp(dylib.input->mappedFile.mh->installName(), "/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation") ) {
                // <rdar://problem/69813664> _NSTheOneTruePredicate is incompatible with __DATA_CONST
                supportsDataConst = false;
            } else if ( !strcmp(dylib.input->mappedFile.mh->installName(), "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation") ||
                       !strcmp(dylib.input->mappedFile.mh->installName(), "/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation") ) {
               // rdar://74112547 CF writes to kCFNull constant object
               supportsDataConst = false;
            } else if ( !strcmp(dylib.input->mappedFile.mh->installName(), "/usr/lib/libcrypto.0.9.7.dylib") ||
                        !strcmp(dylib.input->mappedFile.mh->installName(), "/usr/lib/libcrypto.0.9.8.dylib") ) {
              // rdar://77149283 libcrypto.0.9.8.dylib writes to __DATA_CONST
              supportsDataConst = false;
            }

            // Don't use data const for dylibs containing resolver functions.  This will be fixed in ld64 by moving their pointer atoms to __DATA
            if ( supportsDataConst && endsWith(segInfo.segName, "_CONST") ) {
                dylib.input->mappedFile.mh->forEachExportedSymbol(_diagnostics,
                                                                  ^(const char* symbolName, uint64_t imageOffset, uint64_t flags, uint64_t other, const char* importName, bool& expStop) {
                    if ( (flags & EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER ) != 0 ) {
                        _diagnostics.verbose("%s: preventing use of __DATA_CONST due to resolvers\n", dylib.dylibID.c_str());
                        supportsDataConst = false;
                        expStop = true;
                    }
                });
            }

            // If we are still allowed to use __DATA_CONST, then make sure that we are not using pointer based method lists.  These may not be written in libobjc due
            // to uniquing or sorting (as those are done in the builder), but clients can still call setIMP to mutate them.
            if ( supportsDataConst && endsWith(segInfo.segName, "_CONST") ) {
                uint64_t segStartVMAddr = segInfo.vmAddr;
                uint64_t segEndVMAddr = segInfo.vmAddr + segInfo.vmSize;

                auto vmAddrConverter = dylib.input->mappedFile.mh->makeVMAddrConverter(false);
                const uint32_t pointerSize = dylib.input->mappedFile.mh->pointerSize();
                const uint64_t loadAddress = dylib.input->mappedFile.mh->preferredLoadAddress();

                __block bool foundPointerBasedMethodList = false;
                auto visitMethodList = ^(uint64_t methodListVMAddr) {
                    if ( foundPointerBasedMethodList )
                        return;
                    if ( methodListVMAddr == 0 )
                        return;
                    // Ignore method lists in other segments
                    if ( (methodListVMAddr < segStartVMAddr) || (methodListVMAddr >= segEndVMAddr) )
                        return;
                    uint64_t methodListRuntimeOffset = methodListVMAddr - loadAddress;
                    bool isRelativeMethodList = dylib.input->mappedFile.mh->objcMethodListIsRelative(methodListRuntimeOffset);
                    if ( !isRelativeMethodList )
                        foundPointerBasedMethodList = true;
                };

                auto visitClass = ^(uint64_t classVMAddr,
                                    uint64_t classSuperclassVMAddr, uint64_t classDataVMAddr,
                                    const dyld3::MachOAnalyzer::ObjCClassInfo& objcClass, bool isMetaClass,
                                    bool& stopClass) {
                    visitMethodList(objcClass.baseMethodsVMAddr(pointerSize));
                };

                auto visitCategory = ^(uint64_t categoryVMAddr,
                                       const dyld3::MachOAnalyzer::ObjCCategory& objcCategory,
                                       bool& stopCategory) {
                    visitMethodList(objcCategory.instanceMethodsVMAddr);
                    visitMethodList(objcCategory.classMethodsVMAddr);
                };

                // Walk the class list
                Diagnostics classDiag;
                dylib.input->mappedFile.mh->forEachObjCClass(classDiag, vmAddrConverter, visitClass);

                // Walk the category list
                Diagnostics categoryDiag;
                dylib.input->mappedFile.mh->forEachObjCCategory(categoryDiag, vmAddrConverter, visitCategory);

                // Note we don't walk protocols as they don't have an IMP to set

                if ( foundPointerBasedMethodList ) {
                    _diagnostics.verbose("%s: preventing use of read-only %s due to pointer based method list\n", dylib.dylibID.c_str(), segInfo.segName);
                    supportsDataConst = false;
                }
            }

            // __AUTH_CONST
            if ( strcmp(segInfo.segName, "__AUTH_CONST") == 0 ) {
                dylibSegmentTypes.push_back(supportsDataConst ? authConstSegment : authConstWorkaroundSegment);
                return;
            }

            // __DATA_CONST
            if ( (strcmp(segInfo.segName, "__DATA_CONST") == 0) || (strcmp(segInfo.segName, "__OBJC_CONST") == 0) ) {
                if ( authSegmentIndices.count(segInfo.segIndex) ) {
                    // _diagnostics.verbose("%s: treating authenticated %s as __AUTH_CONST\n", dylib.dylibID.c_str(), segInfo.segName);
                    dylibSegmentTypes.push_back(supportsDataConst ? SegmentType::authConst : SegmentType::authConstWorkarounds);
                } else {
                    dylibSegmentTypes.push_back(supportsDataConst ? SegmentType::dataConst : SegmentType::dataConstWorkarounds);
                }
                return;
            }

            // __DATA_DIRTY
            if ( strcmp(segInfo.segName, "__DATA_DIRTY") == 0 ) {
                if ( authSegmentIndices.count(segInfo.segIndex) ) {
                    dylibSegmentTypes.push_back(SegmentType::authDirty);
                } else {
                    dylibSegmentTypes.push_back(SegmentType::dataDirty);
                }
                return;
            }

            // __AUTH
            if ( strcmp(segInfo.segName, "__AUTH") == 0 ) {
                dylibSegmentTypes.push_back(authSegment);
                return;
            }

            // DATA
            if ( authSegmentIndices.count(segInfo.segIndex) ) {
                // _diagnostics.verbose("%s: treating authenticated %s as __AUTH\n", dylib.dylibID.c_str(), segInfo.segName);
                dylibSegmentTypes.push_back(SegmentType::auth);
            } else {
                dylibSegmentTypes.push_back(SegmentType::data);
            }
        });
    }

    auto processDylibSegments = ^(SegmentType onlyType, Region& region) {
        for (size_t unsortedDylibIndex = 0; unsortedDylibIndex != dylibCount; ++unsortedDylibIndex) {
            size_t dylibIndex = unsortedDylibIndex;
            if ( (onlyType == SegmentType::dataDirty) || (onlyType == SegmentType::authDirty) )
                dylibIndex = dirtyDataSortIndexes[dylibIndex];

            DylibInfo& dylib = (*subCacheImagesBlockPointer)[dylibIndex];
            const std::vector<SegmentType>& dylibSegmentTypes = segmentTypes[dylibIndex];
            const uint64_t textSegVmAddr = textSegVmAddrs[dylibIndex];
            bool forcePageAlignedData = false;
            if ( (_options.platform == dyld3::Platform::macOS) && (onlyType == SegmentType::data) ) {
                forcePageAlignedData = dylib.input->mappedFile.mh->hasUnalignedPointerFixups();
                //if ( forcePageAlignedData )
                //    warning("unaligned pointer in %s\n", dylib.input->mappedFile.runtimePath.c_str());
            }

            dylib.input->mappedFile.mh->forEachSegment(^(const dyld3::MachOFile::SegmentInfo& segInfo, bool& stop) {
                if ( dylibSegmentTypes[segInfo.segIndex] != onlyType )
                    return;

                // We may have coalesced the sections at the end of this segment.  In that case, shrink the segment to remove them.
                __block size_t sizeOfSections = 0;
                __block bool foundCoalescedSection = false;
                dylib.input->mappedFile.mh->forEachSection(^(const dyld3::MachOAnalyzer::SectionInfo &sectInfo, bool malformedSectionRange, bool &stopSection) {
                    if (strcmp(sectInfo.segInfo.segName, segInfo.segName) != 0)
                        return;
                    if ( dylib.textCoalescer.sectionWasCoalesced(segInfo.segName, sectInfo.sectName)) {
                        foundCoalescedSection = true;
                    } else {
                        sizeOfSections = sectInfo.sectAddr + sectInfo.sectSize - segInfo.vmAddr;
                    }
                });
                if (!foundCoalescedSection)
                    sizeOfSections = segInfo.sizeOfSections;

                if ( !forcePageAlignedData ) {
                    // Pack __DATA segments
                    addr = align(addr, segInfo.p2align);
                }
                else {
                    // Keep __DATA segments 4K or more aligned
                    addr = align(addr, std::max((int)segInfo.p2align, (int)12));
                }

                size_t copySize = std::min((size_t)segInfo.fileSize, (size_t)sizeOfSections);
                uint64_t offsetInRegion = addr - region.unslidLoadAddress;
                SegmentMappingInfo loc;
                loc.srcSegment             = (uint8_t*)dylib.input->mappedFile.mh + segInfo.vmAddr - textSegVmAddr;
                loc.segName                = segInfo.segName;
                loc.dstSegment             = region.buffer + offsetInRegion;
                loc.dstCacheUnslidAddress  = addr;
                loc.dstCacheFileOffset     = (uint32_t)(region.cacheFileOffset + offsetInRegion);
                loc.dstCacheSegmentSize    = (uint32_t)sizeOfSections;
                loc.dstCacheFileSize       = (uint32_t)copySize;
                loc.copySegmentSize        = (uint32_t)copySize;
                loc.srcSegmentIndex        = segInfo.segIndex;
                dylib.cacheLocation.push_back(loc);
                addr += loc.dstCacheSegmentSize;
            });
        }

        // align region end
        addr = align(addr, _archLayout->sharedRegionAlignP2);
    };

    struct DataRegion {
        const char*                 regionName;
        SegmentType                 dataSegment;
        std::optional<SegmentType>  dirtySegment;
        // Note this is temporary as once all platforms/archs support __DATA_CONST, we can move to a DataRegion just for CONST
        std::optional<SegmentType>  dataConstSegment;
        bool                        addCFStrings;
        bool                        addObjCRW;
    };
    std::vector<DataRegion> dataRegions;

    bool addObjCRWToData = isObjCSubCache && !supportsAuthFixups;
    bool addObjCRWToAuth = isObjCSubCache && supportsAuthFixups;
    bool addCFStrings = isObjCSubCache;
    DataRegion dataWriteRegion  = { "__DATA",       SegmentType::data,      SegmentType::dataDirty, SegmentType::dataConstWorkarounds, false,          addObjCRWToData  };
    DataRegion dataConstRegion  = { "__DATA_CONST", SegmentType::dataConst, {},                     {},                                addCFStrings,   false,           };
    DataRegion authWriteRegion  = { "__AUTH",       SegmentType::auth,      SegmentType::authDirty, SegmentType::authConstWorkarounds, false,          addObjCRWToAuth, };
    DataRegion authConstRegion  = { "__AUTH_CONST", SegmentType::authConst, {},                     {},                                false,          false,           };
    dataRegions.push_back(dataConstRegion);
    dataRegions.push_back(dataWriteRegion);
    if ( supportsAuthFixups ) {
        dataRegions.push_back(authWriteRegion);
        dataRegions.push_back(authConstRegion);
    }

    for (DataRegion& dataRegion : dataRegions)
    {
        // We only need padding before __DATA
        if ( !strcmp(_archLayout->archName, "arm64") || !strcmp(_archLayout->archName, "arm64e") || !strcmp(_archLayout->archName, "sim-arm64") ) {
            if (strcmp(dataRegion.regionName, "__DATA") == 0) {
                addr = align((addr + _archLayout->sharedRegionPadding), _archLayout->sharedRegionAlignP2);
            }
        }

        Region region;
        region.buffer               = (uint8_t*)_fullAllocatedBuffer + addr - _archLayout->sharedMemoryStart;
        region.bufferSize           = 0;
        region.sizeInUse            = 0;
        region.unslidLoadAddress    = addr;
        region.cacheFileOffset      = nextRegionFileOffset;
        region.name                 = dataRegion.regionName;
        region.initProt             = endsWith(dataRegion.regionName, "_CONST") ? VM_PROT_READ : (VM_PROT_READ | VM_PROT_WRITE);
        region.maxProt              = VM_PROT_READ | VM_PROT_WRITE;

        // layout all __DATA_DIRTY segments, sorted (FIXME)
        if (dataRegion.dirtySegment.has_value()) {
            processDylibSegments(*dataRegion.dirtySegment, region);
        }

        processDylibSegments(dataRegion.dataSegment, region);

        // When __DATA_CONST is not its own DataRegion, we fold it in to the __DATA DataRegion
        if (dataRegion.dataConstSegment.has_value())
            processDylibSegments(*dataRegion.dataConstSegment, region);

        // Make space for the cfstrings
        if ( (dataRegion.addCFStrings) && (_coalescedText.cfStrings.bufferSize != 0) ) {
            // Keep __DATA segments 4K or more aligned
            addr = align(addr, 12);
            uint64_t offsetInRegion = addr - region.unslidLoadAddress;

            CacheCoalescedText::CFSection& cacheSection = _coalescedText.cfStrings;
            cacheSection.bufferAddr         = region.buffer + offsetInRegion;
            cacheSection.bufferVMAddr       = addr;
            cacheSection.cacheFileOffset    = region.cacheFileOffset + offsetInRegion;
            addr += cacheSection.bufferSize;
        }

         if ( dataRegion.addObjCRW ) {
            // reserve space for objc r/w optimization tables
            _objcReadWriteBufferSizeAllocated = objcRWSize;
            addr = align(addr, 4); // objc r/w section contains pointer and must be at least pointer align
            _objcReadWriteBuffer = region.buffer + (addr - region.unslidLoadAddress);
            _objcReadWriteFileOffset = (uint32_t)((_objcReadWriteBuffer - region.buffer) + region.cacheFileOffset);
            addr += _objcReadWriteBufferSizeAllocated;
        }

        // align region end
        addr = align(addr, _archLayout->sharedRegionAlignP2);

        // align DATA region end
        uint64_t endDataAddress = addr;
        region.bufferSize   = endDataAddress - region.unslidLoadAddress;
        region.sizeInUse    = region.bufferSize;

        subCache._dataRegions.push_back(region);
        nextRegionFileOffset = region.cacheFileOffset + region.sizeInUse;
    }

    // Sanity check that we didn't put the same segment in 2 different ranges
    for (DylibInfo& dylib : subCacheImages) {
        std::unordered_set<uint64_t> seenSegmentIndices;
        for (SegmentMappingInfo& segmentInfo : dylib.cacheLocation) {
            if ( seenSegmentIndices.count(segmentInfo.srcSegmentIndex) != 0 ) {
                _diagnostics.error("%s segment %s was duplicated in layout",
                                   dylib.input->mappedFile.mh->installName(), segmentInfo.segName);
                return;
            }
            seenSegmentIndices.insert(segmentInfo.srcSegmentIndex);
        }
    }

    cacheFileOffset = subCache.lastDataRegion()->cacheFileOffset + subCache.lastDataRegion()->sizeInUse;
}

void SharedCacheBuilder::assignReadOnlySegmentAddresses(SubCache& subCache, uint64_t& addr, uint64_t& cacheFileOffset)
{
    // Skip subCache's which don't contain DATA or LINKEDIT
    if ( (subCache._linkeditNumDylibs == 0) && (subCache._dataNumDylibs == 0) )
        return;

    dyld3::Array<DylibInfo> cacheImages(_sortedDylibs.data(), _sortedDylibs.size(), _sortedDylibs.size());
    dyld3::Array<DylibInfo> subCacheImages = cacheImages.subArray(subCache._linkeditFirstDylibIndex, subCache._linkeditNumDylibs);

    Region& readOnlyRegion = subCache._readOnlyRegion.emplace();

    // start read-only region
    readOnlyRegion.buffer               = (uint8_t*)_fullAllocatedBuffer + addr - _archLayout->sharedMemoryStart;
    readOnlyRegion.bufferSize           = 0;
    readOnlyRegion.sizeInUse            = 0;
    readOnlyRegion.unslidLoadAddress    = addr;
    readOnlyRegion.cacheFileOffset      = cacheFileOffset;

    // reserve space for kernel ASLR slide info at start of r/o region
    if ( _options.cacheSupportsASLR ) {
        size_t slideInfoSize = sizeof(dyld_cache_slide_info);
        slideInfoSize = std::max(slideInfoSize, sizeof(dyld_cache_slide_info2));
        slideInfoSize = std::max(slideInfoSize, sizeof(dyld_cache_slide_info3));
        slideInfoSize = std::max(slideInfoSize, sizeof(dyld_cache_slide_info4));
        // We need one slide info header per data region, plus enough space for that regions pages
        // Each region will also be padded to a page-size so that the kernel can wire it.
        for (Region& region : subCache._dataRegions) {
            uint64_t offsetInRegion = addr - readOnlyRegion.unslidLoadAddress;
            region.slideInfoBuffer = readOnlyRegion.buffer + offsetInRegion;
            region.slideInfoBufferSizeAllocated = align(slideInfoSize + (region.sizeInUse/4096) * _archLayout->slideInfoBytesPerPage + 0x4000, _archLayout->sharedRegionAlignP2);
            region.slideInfoFileOffset = readOnlyRegion.cacheFileOffset + offsetInRegion;
            addr += region.slideInfoBufferSizeAllocated;
        }

        addr = align(addr, 14);
    }

    // Only scan dylibs if we have LINKEDIT for them
    if ( subCache._linkeditNumDylibs != 0 ) {

        // layout all read-only (but not LINKEDIT) segments
        for (DylibInfo& dylib : subCacheImages) {
            __block uint64_t textSegVmAddr = 0;
            dylib.input->mappedFile.mh->forEachSegment(^(const dyld3::MachOFile::SegmentInfo& segInfo, bool& stop) {
                if ( strcmp(segInfo.segName, "__TEXT") == 0 )
                    textSegVmAddr = segInfo.vmAddr;
                if ( segInfo.protections != VM_PROT_READ )
                    return;
                if ( strcmp(segInfo.segName, "__LINKEDIT") == 0 )
                    return;

                // Keep segments segments 4K or more aligned
                addr = align(addr, std::max((int)segInfo.p2align, (int)12));
                uint64_t offsetInRegion = addr - readOnlyRegion.unslidLoadAddress;
                SegmentMappingInfo loc;
                loc.srcSegment             = (uint8_t*)dylib.input->mappedFile.mh + segInfo.vmAddr - textSegVmAddr;
                loc.segName                = segInfo.segName;
                loc.dstSegment             = readOnlyRegion.buffer + offsetInRegion;
                loc.dstCacheUnslidAddress  = addr;
                loc.dstCacheFileOffset     = (uint32_t)(readOnlyRegion.cacheFileOffset + offsetInRegion);
                loc.dstCacheSegmentSize    = (uint32_t)align(segInfo.sizeOfSections, 12);
                loc.dstCacheFileSize       = (uint32_t)segInfo.sizeOfSections;
                loc.copySegmentSize        = (uint32_t)segInfo.sizeOfSections;
                loc.srcSegmentIndex        = segInfo.segIndex;
                dylib.cacheLocation.push_back(loc);
                addr += loc.dstCacheSegmentSize;
            });
        }

        // layout all LINKEDIT segments (after other read-only segments), aligned to 16KB
        addr = align(addr, 14);
        subCache._nonLinkEditReadOnlySize =  addr - readOnlyRegion.unslidLoadAddress;
        for (DylibInfo& dylib : subCacheImages) {
            __block uint64_t textSegVmAddr = 0;
            dylib.input->mappedFile.mh->forEachSegment(^(const dyld3::MachOFile::SegmentInfo& segInfo, bool& stop) {
                if ( strcmp(segInfo.segName, "__TEXT") == 0 )
                    textSegVmAddr = segInfo.vmAddr;
                if ( segInfo.protections != VM_PROT_READ )
                    return;
                if ( strcmp(segInfo.segName, "__LINKEDIT") != 0 )
                    return;
                // Keep segments segments 4K or more aligned
                addr = align(addr, std::max((int)segInfo.p2align, (int)12));
                size_t copySize = std::min((size_t)segInfo.fileSize, (size_t)segInfo.sizeOfSections);
                uint64_t offsetInRegion = addr - readOnlyRegion.unslidLoadAddress;
                SegmentMappingInfo loc;
                loc.srcSegment             = (uint8_t*)dylib.input->mappedFile.mh + segInfo.vmAddr - textSegVmAddr;
                loc.segName                = segInfo.segName;
                loc.dstSegment             = readOnlyRegion.buffer + offsetInRegion;
                loc.dstCacheUnslidAddress  = addr;
                loc.dstCacheFileOffset     = (uint32_t)(readOnlyRegion.cacheFileOffset + offsetInRegion);
                loc.dstCacheSegmentSize    = (uint32_t)align(segInfo.sizeOfSections, 12);
                loc.dstCacheFileSize       = (uint32_t)copySize;
                loc.copySegmentSize        = (uint32_t)copySize;
                loc.srcSegmentIndex        = segInfo.segIndex;
                dylib.cacheLocation.push_back(loc);
                addr += loc.dstCacheSegmentSize;
            });
        }

        // Add some more padding.  No idea why
        addr += 0x100000;
    }

    // align r/o region end
    addr = align(addr, _archLayout->sharedRegionAlignP2);

    uint64_t endReadOnlyAddress = addr;
    readOnlyRegion.bufferSize  = endReadOnlyAddress - readOnlyRegion.unslidLoadAddress;
    readOnlyRegion.sizeInUse   = readOnlyRegion.bufferSize;

    cacheFileOffset += readOnlyRegion.sizeInUse;
}

void SharedCacheBuilder::assignSegmentAddresses(uint64_t objcROSize, uint64_t objcRWSize, uint64_t swiftROSize)
{
    uint64_t addr = _archLayout->sharedMemoryStart;
    for (SubCache& subCache : _subCaches) {
        // calculate size of header info and where first dylib's mach_header should start
        size_t startOffset = sizeof(dyld_cache_header) + DyldSharedCache::MaxMappings * sizeof(dyld_cache_mapping_info);
        startOffset += DyldSharedCache::MaxMappings * sizeof(dyld_cache_mapping_and_slide_info);
        startOffset += sizeof(dyld_cache_image_info) * _sortedDylibs.size();
        startOffset += sizeof(dyld_cache_image_text_info) * _sortedDylibs.size();
        for (const DylibInfo& dylib : _sortedDylibs) {
            startOffset += (strlen(dylib.input->mappedFile.mh->installName()) + 1);
        }

        //fprintf(stderr, "%s total header size = 0x%08lX\n", _options.archName.c_str(), startOffset);
        startOffset = align(startOffset, 12);

        // HACK!: Rebase v4 assumes that values below 0x8000 are not pointers (encoding as offsets from the cache header).
        // If using a minimal cache, we need to pad out the cache header to make sure a pointer doesn't fall within that range
#if SUPPORT_ARCH_arm64_32 || SUPPORT_ARCH_armv7k
        if ( _options.cacheSupportsASLR && !_archLayout->is64 ) {
            if ( _archLayout->pointerDeltaMask == 0xC0000000 )
                startOffset = std::max(startOffset, (size_t)0x8000);
        }
#endif

        uint64_t cacheFileOffset = 0;

        // __TEXT
        assignReadExecuteSegmentAddresses(subCache, addr, cacheFileOffset, startOffset, objcROSize, swiftROSize);

        // If we are using split caches, and there's no DATA or LINKEDIT in this cache, then jump straight to the
        // next cache file, without aligning the address.  That allows the next cache's __TEXT to be mapped in immediately
        // after this cache, with no gap for page tables.
        // TODO: Double check with the VM team that N __TEXT regions mapped contiguously can share page tables
        if ( (subCache._dataNumDylibs == 0) && (subCache._linkeditNumDylibs == 0) )
            continue;

        // __DATA
        if ( _archLayout->sharedRegionsAreDiscontiguous ) {
            // x86 simulators don't use subCaches.  Use the layout we know we can back-deploy
            if ( _options.forSimulator || startsWith(_archLayout->archName, "small-") ) {
                addr = SIM_DISCONTIGUOUS_RW;
            } else {
                assert(subCache._readExecuteRegion.bufferSize <= DISCONTIGUOUS_REGION_SIZE);
                addr = subCache._readExecuteRegion.unslidLoadAddress + DISCONTIGUOUS_REGION_SIZE;
            }
        } else if ( subCache._addPaddingAfterText ) {
            // In split caches, we split the DATA/LINKEDIT cache file.  But the DATA file still needs a
            // piece of LINKEDIT at the end for slide info.  In the subsequent subcache file, we are only storing
            // LINKEDIT, and so we don't want to align again.
            addr = align((addr + _archLayout->sharedRegionPadding), _archLayout->sharedRegionAlignP2);
        }
        assignDataSegmentAddresses(subCache, addr, cacheFileOffset, objcRWSize);

        // LINKEDIT
        if ( _archLayout->sharedRegionsAreDiscontiguous ) {
            // x86 simulators don't use subCaches.  Use the layout we know we can back-deploy
            if ( _options.forSimulator || startsWith(_archLayout->archName, "small-") ) {
                addr = SIM_DISCONTIGUOUS_RO;
            } else {
                if ( const Region* dataRegion = subCache.firstDataRegion() ) {
                    assert(subCache.dataRegionsTotalSize() <= DISCONTIGUOUS_REGION_SIZE);
                    addr = dataRegion->unslidLoadAddress + DISCONTIGUOUS_REGION_SIZE;

                    // Add space for Rosetta.  There should be plenty of space up to half the region, so that we have enough slide
                    assert(subCache.dataRegionsTotalSize() <= _archLayout->subCacheTextLimit);
                    subCache._rosettaReadWriteAddr = dataRegion->unslidLoadAddress + subCache.dataRegionsTotalSize();
                    subCache._rosettaReadWriteSize = (_archLayout->subCacheTextLimit - subCache.dataRegionsTotalSize());
                }
            }
        } else if ( subCache._addPaddingAfterData ) {
            // In split caches, we split the DATA/LINKEDIT cache file.  But the DATA file still needs a
            // piece of LINKEDIT at the end for slide info.  In the subsequent subcache file, we are only storing
            // LINKEDIT, and so we don't want to align again.
            addr = align((addr + _archLayout->sharedRegionPadding), _archLayout->sharedRegionAlignP2);
        }
        assignReadOnlySegmentAddresses(subCache, addr, cacheFileOffset);

        // Align the buffer for the next subCache, in case we have one
        if ( _archLayout->sharedRegionsAreDiscontiguous && subCache._readOnlyRegion.has_value()) {
            assert(subCache._readOnlyRegion->bufferSize <= DISCONTIGUOUS_REGION_SIZE);
            addr = subCache._readOnlyRegion->unslidLoadAddress + DISCONTIGUOUS_REGION_SIZE;
            addr += DISCONTIGUOUS_REGION_SIZE;
            // Add space for Rosetta.  We add another 1GB, then give Rosetta everything from the end of LINKEDIT to the end of the allocated space
            // Note we update the address and size later, once we have optimized LINKEDIT
            subCache._rosettaReadOnlyAddr = subCache._readOnlyRegion->unslidLoadAddress + subCache._readOnlyRegion->sizeInUse;
            subCache._rosettaReadOnlySize = (addr - subCache._rosettaReadOnlyAddr);
        }
        //fprintf(stderr, "RX region=%p -> %p, logical addr=0x%llX\n", _readExecuteRegion.buffer, _readExecuteRegion.buffer+_readExecuteRegion.bufferSize, _readExecuteRegion.unslidLoadAddress);
        //fprintf(stderr, "RW region=%p -> %p, logical addr=0x%llX\n", readWriteRegion.buffer,   readWriteRegion.buffer+readWriteRegion.bufferSize, readWriteRegion.unslidLoadAddress);
        //fprintf(stderr, "RO region=%p -> %p, logical addr=0x%llX\n", _readOnlyRegion.buffer,    _readOnlyRegion.buffer+_readOnlyRegion.bufferSize, _readOnlyRegion.unslidLoadAddress);
    }

    // sort SegmentMappingInfo for each image to be in the same order as original segments
    for (DylibInfo& dylib : _sortedDylibs) {
        std::sort(dylib.cacheLocation.begin(), dylib.cacheLocation.end(), [&](const SegmentMappingInfo& a, const SegmentMappingInfo& b) {
            return a.srcSegmentIndex < b.srcSegmentIndex;
        });
    }
}

// Return the total size of the data regions, including padding between them.
// Note this assumes they are contiguous, or that we don't care about including
// additional space between them.
uint64_t SharedCacheBuilder::SubCache::dataRegionsTotalSize() const {
    const Region* firstRegion = nullptr;
    const Region* lastRegion = nullptr;
    for (const Region& region : _dataRegions) {
        if ( (firstRegion == nullptr) || (region.buffer < firstRegion->buffer) )
            firstRegion = &region;
        if ( (lastRegion == nullptr) || (region.buffer > lastRegion->buffer) )
            lastRegion = &region;
    }
    return (lastRegion->buffer - firstRegion->buffer) + lastRegion->sizeInUse;
}


// Return the total size of the data regions, excluding padding between them
uint64_t SharedCacheBuilder::SubCache::dataRegionsSizeInUse() const {
    size_t size = 0;
    for (const Region& dataRegion : _dataRegions)
        size += dataRegion.sizeInUse;
    return size;
}

// Return the earliest data region by address
const CacheBuilder::Region* SharedCacheBuilder::SubCache::firstDataRegion() const {
    const Region* firstRegion = nullptr;
    for (const Region& region : _dataRegions) {
        if ( (firstRegion == nullptr) || (region.buffer < firstRegion->buffer) )
            firstRegion = &region;
    }
    return firstRegion;
}

// Return the lateset data region by address
const CacheBuilder::Region* SharedCacheBuilder::SubCache::lastDataRegion() const {
    const Region* lastRegion = nullptr;
    for (const Region& region : _dataRegions) {
        if ( (lastRegion == nullptr) || (region.buffer > lastRegion->buffer) )
            lastRegion = &region;
    }
    return lastRegion;
}

uint64_t SharedCacheBuilder::SubCache::highestVMAddress() const {
    if ( _readOnlyRegion.has_value() )
        return _readOnlyRegion->unslidLoadAddress + _readOnlyRegion->sizeInUse;
    if ( const Region* region = lastDataRegion() )
        return region->unslidLoadAddress + region->sizeInUse;
    return _readExecuteRegion.unslidLoadAddress + _readExecuteRegion.sizeInUse;
}

uint64_t SharedCacheBuilder::SubCache::highestFileOffset() const {
    if ( _readOnlyRegion.has_value() )
        return _readOnlyRegion->cacheFileOffset + _readOnlyRegion->sizeInUse;
    if ( const Region* region = lastDataRegion() )
        return region->cacheFileOffset + region->sizeInUse;
    return _readExecuteRegion.cacheFileOffset + _readExecuteRegion.sizeInUse;
}

SharedCacheBuilder::DylibSymbolClients::dyld_cache_patchable_location::dyld_cache_patchable_location(uint64_t cacheOff, MachOLoaded::PointerMetaData pmd, uint64_t addend) {
    this->cacheOffset           = cacheOff;
    this->high7                 = pmd.high8 >> 1;
    this->addend                = addend;
    this->authenticated         = pmd.authenticated;
    this->usesAddressDiversity  = pmd.usesAddrDiversity;
    this->key                   = pmd.key;
    this->discriminator         = pmd.diversity;
    // check for truncations
    assert(this->cacheOffset == cacheOff);
    assert(this->addend == addend);
    assert((this->high7 << 1) == pmd.high8);
}


// Note: this is called twice.  First to run applyFixups() which binds all DATA pointers
// Then again after the LINKEDIT is optimized to create the PrebuiltLoaderSet.
void SharedCacheBuilder::buildDylibJITLoaders(dyld4::RuntimeState& state, const std::vector<DyldSharedCache::FileAlias>& aliases, std::vector<JustInTimeLoader*>& jitLoaders)
{
    DyldSharedCache* cache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;
    __block std::unordered_map<std::string, JustInTimeLoader*> loadersMap;
    __block std::unordered_map<std::string, uint32_t>          loadersIndexMap;
    // make one pass to build the map so we can detect unzippered twins
    cache->forEachDylib(^(const dyld3::MachOAnalyzer* ma, const char* installName, uint32_t imageIndex, uint64_t inode, uint64_t mtime, bool& stop) {
        loadersIndexMap[installName] = imageIndex;
    });
    cache->forEachDylib(^(const dyld3::MachOAnalyzer* ma, const char* installName, uint32_t imageIndex, uint64_t inode, uint64_t mtime, bool& stop) {
        //printf("mh=%p, %s\n", mh, installName);
        bool     catalystTwin = false;
        uint32_t macTwinIndex = 0;
        if ( strncmp(installName, "/System/iOSSupport/", 19) == 0 ) {
            if ( loadersIndexMap.count(&installName[18]) != 0 ) {
                catalystTwin = true;
                macTwinIndex = loadersIndexMap[&installName[18]];
            }
        }
        // inode and mtime are only valid if dylibs will remain on disk, ie, the simulator cache builder case
        bool fileIDValid = !_options.dylibsRemovedDuringMastering;
        dyld4::FileID fileID(inode, mtime, fileIDValid);
        JustInTimeLoader* jitLoader = JustInTimeLoader::makeJustInTimeLoaderDyldCache(state, ma, installName, imageIndex, fileID, catalystTwin, macTwinIndex);
        loadersMap[installName] = jitLoader;
        jitLoaders.push_back(jitLoader);
    });
    for (const DyldSharedCache::FileAlias& alias : aliases) {
         JustInTimeLoader* a = loadersMap[alias.aliasPath];
         JustInTimeLoader* r = loadersMap[alias.realPath];
         if ( a != nullptr )
            loadersMap[alias.realPath] = a;
         else if ( r != nullptr ) {
            loadersMap[alias.aliasPath] = r;
            _dylibAliases.insert(alias.aliasPath);
        }
    }

    Loader::LoadOptions::Finder loaderFinder = ^(Diagnostics& diag, dyld3::Platform, const char* loadPath, const dyld4::Loader::LoadOptions& options) {
        auto pos = loadersMap.find(loadPath);
        if ( pos != loadersMap.end() ) {
            return (const Loader*)pos->second;
        }
        // Handle symlinks containing relative paths.  Unfortunately the only way to do this right now is with the fake file system
        char buffer[PATH_MAX];
        if ( _fileSystem.getRealPath(loadPath, buffer) ) {
            pos = loadersMap.find(buffer);
            if ( pos != loadersMap.end() ) {
                return (const Loader*)pos->second;
            }
        }
        if ( !options.canBeMissing )
            diag.error("dependent dylib '%s' not found", loadPath);
        return (const Loader*)nullptr;
    };

    Loader::LoadOptions options;
    options.staticLinkage   = true;
    options.launching       = true;
    options.canBeDylib      = true;
    options.finder          = loaderFinder;
    Diagnostics loadDiag;
    for (const Loader* ldr : state.loaded) {
        ((Loader*)ldr)->loadDependents(loadDiag, state, options);
        if ( loadDiag.hasError() ) {
            _diagnostics.error("%s, loading dependents of %s", loadDiag.errorMessageCStr(), ldr->path());
            return;
        }
    }

}


static bool hasHigh8(uint64_t addend)
{
    // distinguish negative addend from TBI
    if ( (addend >> 56) == 0 )
        return false;
    return ( (addend >> 48) != 0xFFFF );
}

static void forEachDylibFixup(Diagnostics& diag, RuntimeState& state, const Loader* ldr, const MachOAnalyzer* ma, Loader::FixUpHandler fixup, Loader::CacheWeakDefOverride patcher)
{
    const uint64_t prefLoadAddr = ma->preferredLoadAddress();
    if ( ma->hasChainedFixups() ) {
        // build targets table
        typedef std::pair<Loader::ResolvedSymbol, uint64_t> Target;
        __block std::vector<Target> targets;
        ma->forEachChainedFixupTarget(diag, ^(int libOrdinal, const char* symbolName, uint64_t addend, bool weakImport, bool& stop) {
            //log("Loader::forEachFixup(): libOrd=%d, symbolName=%s\n", libOrdinal, symbolName);
            Loader::ResolvedSymbol target = ldr->resolveSymbol(diag, state, libOrdinal, symbolName, weakImport, false, patcher, true);
            if ( diag.hasError() )
                stop = true;
            target.targetRuntimeOffset += addend;
            targets.push_back({ target, addend });
        });
        if ( diag.hasError() )
            return;

        // walk all chains
        ma->withChainStarts(diag, ma->chainStartsOffset(), ^(const dyld_chained_starts_in_image* startsInfo) {
            ma->forEachFixupInAllChains(diag, startsInfo, false, ^(MachOLoaded::ChainedFixupPointerOnDisk* fixupLoc,
                                                                    const dyld_chained_starts_in_segment* segInfo, bool& fixupsStop) {
                uint64_t fixupOffset = (uint8_t*)fixupLoc - (uint8_t*)ma;
                uint64_t targetOffset;
                uint32_t bindOrdinal;
                int64_t  embeddedAddend;
                MachOLoaded::PointerMetaData pmd(fixupLoc, segInfo->pointer_format);
                if ( fixupLoc->isBind(segInfo->pointer_format, bindOrdinal, embeddedAddend) ) {
                    if ( bindOrdinal < targets.size() ) {
                        const Target& targetInTable = targets[bindOrdinal];
                        uint64_t addend = targetInTable.second;
                        if ( embeddedAddend == 0 ) {
                            if ( hasHigh8(addend) ) {
                                Loader::ResolvedSymbol targetWithoutHigh8 = targetInTable.first;
                                pmd.high8 = (targetInTable.second >> 56);
                                targetWithoutHigh8.targetRuntimeOffset &= 0x00FFFFFFFFFFFFFFULL;
                                addend                                 &= 0x00FFFFFFFFFFFFFFULL;
                                fixup(fixupOffset, addend, pmd, targetWithoutHigh8, fixupsStop);
                            }
                            else {
                                fixup(fixupOffset, addend, pmd, targetInTable.first, fixupsStop);
                            }
                        } else {
                            // pointer on disk encodes extra addend, make pseudo target for that
                            Loader::ResolvedSymbol targetWithAddend = targetInTable.first;
                            targetWithAddend.targetRuntimeOffset += embeddedAddend;
                            addend                               += embeddedAddend;
                            fixup(fixupOffset, addend, pmd, targetWithAddend, fixupsStop);
                        }
                    }
                    else {
                        diag.error("out of range bind ordinal %d (max %lu)", bindOrdinal, targets.size());
                        fixupsStop = true;
                    }
                }
                else if ( fixupLoc->isRebase(segInfo->pointer_format, prefLoadAddr, targetOffset) ) {
                    Loader::ResolvedSymbol rebaseTarget;
                    rebaseTarget.targetLoader               = ldr;
                    rebaseTarget.targetRuntimeOffset        = targetOffset & 0x00FFFFFFFFFFFFFFULL;
                    rebaseTarget.targetSymbolName           = nullptr;
                    rebaseTarget.kind                       = Loader::ResolvedSymbol::Kind::rebase;
                    rebaseTarget.isCode                     = 0;
                    rebaseTarget.isWeakDef                  = 0;
                    fixup(fixupOffset, 0, pmd, rebaseTarget, fixupsStop);
                }
            });
        });
    }
    else {
        // process all rebase opcodes
        const bool is64 = ma->is64();
        ma->forEachRebase(diag, ^(uint64_t runtimeOffset, bool isLazyPointerRebase, bool& stop) {
            uint64_t*                    loc         = (uint64_t*)((uint8_t*)ma + runtimeOffset);
            uint64_t                     locValue    = is64 ? *loc : *((uint32_t*)loc);
            Loader::ResolvedSymbol       rebaseTarget;
            MachOLoaded::PointerMetaData pmd;
            if ( is64 )
                pmd.high8  = (locValue >> 56);
            rebaseTarget.targetLoader               = ldr;
            rebaseTarget.targetRuntimeOffset        = (locValue & 0x00FFFFFFFFFFFFFFULL) - prefLoadAddr;
            rebaseTarget.targetSymbolName           = nullptr;
            rebaseTarget.kind                       = Loader::ResolvedSymbol::Kind::rebase;
            rebaseTarget.isCode                     = 0;
            rebaseTarget.isWeakDef                  = 0;
            fixup(runtimeOffset, 0, pmd, rebaseTarget, stop);
        });
        if ( diag.hasError() )
            return;

        // process all bind opcodes
        __block int                          lastLibOrdinal  = 0xFFFF;
        __block const char*                  lastSymbolName  = nullptr;
        __block uint64_t                     lastAddend      = 0;
        __block Loader::ResolvedSymbol       target;
        __block MachOLoaded::PointerMetaData pmd;
        ma->forEachBind(diag, ^(uint64_t runtimeOffset, int libOrdinal, uint8_t type, const char* symbolName,
                                bool weakImport, bool lazyBind, uint64_t addend, bool& stop) {
            if ( (symbolName == lastSymbolName) && (libOrdinal == lastLibOrdinal) && (addend == lastAddend) ) {
                // same symbol lookup as last location
                fixup(runtimeOffset, addend, pmd, target, stop);
            }
            else {
                target = ldr->resolveSymbol(diag, state, libOrdinal, symbolName, weakImport, lazyBind, patcher, true);
                if ( target.targetLoader != nullptr ) {
                    pmd.high8 = 0;
                    if ( is64 && (addend != 0) && hasHigh8(addend) ) {
                        pmd.high8  = (addend >> 56);
                        target.targetRuntimeOffset &= 0x00FFFFFFFFFFFFFFULL;
                        addend                     &= 0x00FFFFFFFFFFFFFFULL;
                    }
                    else if ( addend != 0 ) {
                        target.targetRuntimeOffset += addend;
                    }
                    fixup(runtimeOffset, addend, pmd, target, stop);
                }
            }
        },
        ^(const char* symbolName) {
        });
    }
    if ( diag.hasError() )
        return;

}

// When this is complete all dylibs in the cache have binds and rebases resolved to be their target's
// unslid address.  The ASLRTracker contains all info to later turn those pointers into chained fixups.
void SharedCacheBuilder::bindDylibs(const MachOAnalyzer* aMainExe, const std::vector<DyldSharedCache::FileAlias>& aliases)
{
    KernelArgs            kernArgs(aMainExe, {"test.exe"}, {}, {});
    SyscallDelegate       osDelegate;
    osDelegate._dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;

    ProcessConfig         config(&kernArgs, osDelegate);
    RuntimeState          state(config);
    RuntimeState*         statePtr = &state;


    // build JITLoaders for all dylibs in cache
    std::vector<JustInTimeLoader*> jitLoaders;
    buildDylibJITLoaders(state, aliases, jitLoaders);
    if ( _diagnostics.hasError() )
        return;

    // Are subCache images guaranteed to be in the same order as the Loader's?
    // FIXME: Otherwise make a map here
    std::vector<CacheBuilder::ASLR_Tracker*> aslrTrackers;
    for (SubCache& subCache : _subCaches) {
        // Skip subCache's which don't contain DATA
        if ( subCache._dataNumDylibs == 0 )
            continue;

        for (uint64_t i = 0; i != subCache._dataNumDylibs; ++i) {
            aslrTrackers.push_back(&subCache._aslrTracker);
        }
    }

    Region& firstReadExecuteRegion = _subCaches.front()._readExecuteRegion;

    // Assume the last SubCache has LINKEDIT
    assert(_subCaches.back()._readOnlyRegion.has_value());
    Region& lastReadOnlyRegion = *(_subCaches.back()._readOnlyRegion);

    // apply fixups to them, which turns all binds into rebases and updates _aslrTracker
    uint32_t dylibIndex = 0;
    for (const Loader* ldr : state.loaded) {
        __block Diagnostics  fixupDiag;
        const MachOAnalyzer* ldrMA        = ldr->analyzer(state);

        CacheBuilder::ASLR_Tracker& aslrTracker = *aslrTrackers[dylibIndex];

        forEachDylibFixup(fixupDiag, state, ldr, ldrMA, ^(uint64_t fixupLocRuntimeOffset, uint64_t addend, MachOLoaded::PointerMetaData pmd, const Loader::ResolvedSymbol& target, bool& stop) {
            uint8_t*  fixupLoc = (uint8_t*)ldrMA + fixupLocRuntimeOffset;
            uint32_t* fixupLoc32 = (uint32_t*)fixupLoc;
            uint64_t* fixupLoc64 = (uint64_t*)fixupLoc;
            uint64_t  targetSymbolOffsetInCache;
            switch ( target.kind ) {
                case Loader::ResolvedSymbol::Kind::rebase:
                    // rebasing already done in AdjustDylibSegments, but if input dylib uses chained fixups, target might not fit
                    if ( _archLayout->is64 ) {
                        if ( pmd.authenticated )
                            aslrTracker.setAuthData(fixupLoc, pmd.diversity, pmd.usesAddrDiversity, pmd.key);
                        if ( pmd.high8 )
                            aslrTracker.setHigh8(fixupLoc, pmd.high8);
                        uint64_t targetVmAddr;
                        if ( aslrTracker.hasRebaseTarget64(fixupLoc, &targetVmAddr) )
                            *fixupLoc64 = targetVmAddr;
                        else {
                            // The runtime offset might be negative.  This can happen in the shared cache builder when coalescing strings.  A rebase
                            // from our dylib might point backwards in to the section where we coalesced the strings.
                            uint64_t targetRuntimeOffset = (uint64_t)((((int64_t)target.targetRuntimeOffset) << 8) >> 8);
                            *fixupLoc64 = (uint8_t*)target.targetLoader->loadAddress(*statePtr) - firstReadExecuteRegion.buffer + targetRuntimeOffset + firstReadExecuteRegion.unslidLoadAddress;
                        }
                    }
                    else {
                        uint32_t targetVmAddr;
                        assert(aslrTracker.hasRebaseTarget32(fixupLoc, &targetVmAddr) && "32-bit archs always store target in side table");
                        *fixupLoc32 = targetVmAddr;
                    }
                    break;
                case Loader::ResolvedSymbol::Kind::bindAbsolute:
                    if ( _archLayout->is64 )
                        *fixupLoc64 = target.targetRuntimeOffset;
                    else
                        *fixupLoc32 = (uint32_t)(target.targetRuntimeOffset);
                    // don't record absolute targets for ASLR
                    aslrTracker.remove(fixupLoc);
                    break;
                case Loader::ResolvedSymbol::Kind::bindToImage:
                    targetSymbolOffsetInCache = (uint8_t*)target.targetLoader->loadAddress(*statePtr) - firstReadExecuteRegion.buffer + target.targetRuntimeOffset - addend;
                    // FIXME: this handler may be called a second time for weak_bind info, which we ignore when building cache
                    aslrTracker.add(fixupLoc);
                    if ( _archLayout->is64 ) {
                        if ( pmd.high8 )
                            aslrTracker.setHigh8(fixupLoc, pmd.high8);
                        if ( pmd.authenticated )
                            aslrTracker.setAuthData(fixupLoc, pmd.diversity, pmd.usesAddrDiversity, pmd.key);
                        *fixupLoc64 = _archLayout->sharedMemoryStart + targetSymbolOffsetInCache + addend;
                    }
                    else {
                        assert(targetSymbolOffsetInCache < (uint64_t)(lastReadOnlyRegion.buffer - firstReadExecuteRegion.buffer) && "offset not into TEXT or DATA of cache file");
                        uint32_t targetVmAddr;
                        if ( aslrTracker.hasRebaseTarget32(fixupLoc, &targetVmAddr) )
                            *fixupLoc32 = targetVmAddr;
                        else
                            *fixupLoc32 = (uint32_t)(_archLayout->sharedMemoryStart + targetSymbolOffsetInCache + addend);
                    }
                    if ( target.isWeakDef )
                        _dylibWeakExports.insert({ target.targetLoader->loadAddress(*statePtr), targetSymbolOffsetInCache });
                    _exportsToName[targetSymbolOffsetInCache] = target.targetSymbolName;

                    DylibSymbolClients& dylibClients = _dylibToItsClients[target.targetLoader->loadAddress(*statePtr)];
                    DylibSymbolClients::Uses& clientUses = dylibClients._clientToUses[ldrMA];
                    clientUses._uses[targetSymbolOffsetInCache].push_back({ (uint64_t)fixupLoc - (uint64_t)firstReadExecuteRegion.buffer, pmd, addend });
                    break;
            }
        },
        ^(uint32_t cachedDylibIndex, uint32_t cachedDylibVMOffset, const Loader::ResolvedSymbol& target) {

        });
        if ( fixupDiag.hasError() ) {
            _diagnostics.error("%s, applying fixups to %s", fixupDiag.errorMessageCStr(), ldr->path());
            return;
        }
        ++dylibIndex;
    }
}

CacheBuilder::Region& SharedCacheBuilder::getSharedCacheReadOnlyRegion() {
    // We always use the first subCache with dylib LINKEDIT to hold additional cache metadata
    // Not all subCache's have LINKEDIT, so we'll find the first one.
    // Note some sub caches may have LINKEDIT which is only used for slide info, ie, the split
    // cache for DATA. That LINKEDIT is not the one we want here.
    for (SubCache& subCache : _subCaches) {
        if ( subCache._linkeditNumDylibs == 0 )
            continue;
        if ( subCache._readOnlyRegion.has_value() )
            return *(subCache._readOnlyRegion);
    }
    // At least 1 subCache must have LINKEDIT
    assert(false);
}


void SharedCacheBuilder::buildDylibsTrie(const std::vector<DyldSharedCache::FileAlias>& aliases, std::unordered_map<std::string, uint32_t>& dylibPathToDylibIndex)
{
    DyldSharedCache* dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;

    // build up all Entries in trie
    __block std::vector<DylibIndexTrie::Entry>   dylibEntrys;
    __block uint32_t                             index = 0;
    dyldCache->forEachImage(^(const mach_header* mh, const char* installName) {
        dylibEntrys.push_back(DylibIndexTrie::Entry(installName, DylibIndex(index)));
        dylibPathToDylibIndex[installName] = index;
        ++index;
    });
    for (const DyldSharedCache::FileAlias& alias : aliases) {
        const auto& pos = dylibPathToDylibIndex.find(alias.realPath);
        if ( pos != dylibPathToDylibIndex.end() ) {
            dylibEntrys.push_back(DylibIndexTrie::Entry(alias.aliasPath.c_str(), pos->second));
        }
    }
    DylibIndexTrie dylibsTrie(dylibEntrys);
    std::vector<uint8_t> trieBytes;
    dylibsTrie.emit(trieBytes);
    while ( (trieBytes.size() % 8) != 0 )
        trieBytes.push_back(0);

    // verify there is room in LINKEDIT for trie
    Region& readOnlyRegion = getSharedCacheReadOnlyRegion();
    size_t freeSpace = readOnlyRegion.bufferSize - readOnlyRegion.sizeInUse;
    if ( trieBytes.size() > freeSpace ) {
        _diagnostics.error("cache buffer too small to hold Trie (buffer size=%lldMB, trie size=%luKB, free space=%ldMB)",
                            _allocatedBufferSize/1024/1024, trieBytes.size()/1024, freeSpace/1024/1024);
        return;
    }

    // copy trie into cache and update header
    dyldCache->header.dylibsTrieAddr = readOnlyRegion.unslidLoadAddress + readOnlyRegion.sizeInUse;
    dyldCache->header.dylibsTrieSize = trieBytes.size();
    ::memcpy(readOnlyRegion.buffer + readOnlyRegion.sizeInUse, &trieBytes[0], trieBytes.size());
    readOnlyRegion.sizeInUse += trieBytes.size();

}

// Builds a PrebuiltLoaderSet for all dylibs in the cache.
// Also builds a trie that maps dylib paths to their index in the cache.
void SharedCacheBuilder::buildDylibsPrebuiltLoaderSet(const MachOAnalyzer* aMain, const std::vector<DyldSharedCache::FileAlias>& aliases)
{
    // build and add to cache a trie that maps dylib paths to dylib index
    std::unordered_map<std::string, uint32_t> dylibPathToDylibIndex;
    buildDylibsTrie(aliases, dylibPathToDylibIndex);

    // need to build patch table before PrebuiltLoaders, so that on macOS PrebuiltLoaders can use patching info for catalyst
    buildPatchTables(dylibPathToDylibIndex);

    // build PrebuiltLoaderSet of all dylibs in cache
    DyldSharedCache*      dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;
    KernelArgs            kernArgs(aMain, {"test.exe"}, {}, {});
    SyscallDelegate       osDelegate;
    osDelegate._dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;
    ProcessConfig         config(&kernArgs, osDelegate);
    RuntimeState          state(config);

    // build JITLoaders for all dylibs in cache
    std::vector<JustInTimeLoader*> jitLoaders;
    buildDylibJITLoaders(state, aliases, jitLoaders);

    // now make a PrebuiltLoaderSet from all the JustInTimeLoaders for all the dylibs in the shared cache
    STACK_ALLOC_ARRAY(const Loader*, allDylibs, state.loaded.size());
    for (const Loader* ldr : state.loaded)
        allDylibs.push_back(ldr);
    _cachedDylibsLoaderSet = dyld4::PrebuiltLoaderSet::makeDyldCachePrebuiltLoaders(_diagnostics, state, dyldCache, allDylibs);
    uint64_t prebuiltLoaderSetSize = _cachedDylibsLoaderSet->size();

    // check for fit
    Region& readOnlyRegion = getSharedCacheReadOnlyRegion();
    size_t freeSpace = readOnlyRegion.bufferSize - readOnlyRegion.sizeInUse;
    if ( prebuiltLoaderSetSize > freeSpace ) {
        _diagnostics.error("cache buffer too small to hold dylib PrebuiltLoaderSet (buffer size=%lldMB, prebuiltLoaderSet size=%lluKB, free space=%ldMB)",
                            _allocatedBufferSize/1024/1024, prebuiltLoaderSetSize/1024, freeSpace/1024/1024);
        return;
    }

    // copy the PrebuiltLoaderSet for dylibs into the cache
    dyldCache->header.dylibsPBLSetAddr = readOnlyRegion.unslidLoadAddress + readOnlyRegion.sizeInUse;
    ::memcpy(readOnlyRegion.buffer + readOnlyRegion.sizeInUse, _cachedDylibsLoaderSet, prebuiltLoaderSetSize);
    readOnlyRegion.sizeInUse += prebuiltLoaderSetSize;
}

void SharedCacheBuilder::buildPatchTables(const std::unordered_map<std::string, uint32_t>& loaderToIndexMap)
{
    DyldSharedCache* dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;

    // build set of functions to never stub-eliminate because tools may need to override them
    std::unordered_set<std::string> alwaysGeneratePatch;
    for (const char* const* p=_s_neverStubEliminateSymbols; *p != nullptr; ++p)
        alwaysGeneratePatch.insert(*p);

    // Add the patches for the image array.
    __block uint64_t numPatchImages             = dyldCache->header.imagesCount;
    __block uint64_t numImageExports            = 0;
    __block uint64_t numPatchClients            = 0;
    __block uint64_t numClientExports           = 0;
    __block uint64_t numPatchLocations          = 0;
    __block uint64_t numPatchExportNameBytes    = 0;

    auto needsPatch = [&](bool dylibNeedsPatching, const dyld3::MachOLoaded* mh,
                          CacheOffset offset) -> bool {
        if (dylibNeedsPatching)
            return true;
        if (_dylibWeakExports.find({ mh, offset }) != _dylibWeakExports.end())
            return true;
        const std::string& exportName = _exportsToName[offset];
        return alwaysGeneratePatch.find(exportName) != alwaysGeneratePatch.end();
    };

    // First calculate how much space we need
    __block std::unordered_map<CacheOffset, uint32_t> exportNameOffsets;
    dyldCache->forEachImage(^(const mach_header* mh, const char* installName) {
        const dyld3::MachOLoaded* ml = (const dyld3::MachOLoaded*)mh;

        // On a customer cache, only store patch locations for interposable dylibs and weak binding
        bool dylibNeedsPatching = dyldCache->isOverridablePath(installName);

        DylibSymbolClients& dylibSymbolClients = _dylibToItsClients[ml];
        for (auto& clientDylibAndUses : dylibSymbolClients._clientToUses) {
            bool clientUsed = false;
            for (auto& targetCacheOffsetAndUses : clientDylibAndUses.second._uses) {
                const CacheOffset& exportCacheOffset = targetCacheOffsetAndUses.first;
                if (!needsPatch(dylibNeedsPatching, ml, exportCacheOffset))
                    continue;

                std::vector<DylibSymbolClients::dyld_cache_patchable_location>& uses = targetCacheOffsetAndUses.second;
                uses.erase(std::unique(uses.begin(), uses.end()), uses.end());
                if ( uses.empty() )
                    continue;

                // We have uses in this client->location->uses list.  Track them
                clientUsed = true;
                ++numClientExports;
                numPatchLocations += uses.size();

                // Track this location as one the target dylib needs to export
                dylibSymbolClients._usedExports.insert(exportCacheOffset);

                // We need space for the name too
                auto itAndInserted = exportNameOffsets.insert({ exportCacheOffset, numPatchExportNameBytes });
                if ( itAndInserted.second ) {
                    // We inserted the name, so make space for it
                    std::string exportName = _exportsToName[exportCacheOffset];
                    numPatchExportNameBytes += exportName.size() + 1;
                }
            }

            // Make space for this client, if it is used
            if ( clientUsed )
                ++numPatchClients;
        }

        // Track how many exports this image needs
        numImageExports += dylibSymbolClients._usedExports.size();
    });

    exportNameOffsets.clear();

    // Now reserve the space
    __block std::vector<dyld_cache_image_patches_v2>        patchImages;
    __block std::vector<dyld_cache_image_export_v2>         imageExports;
    __block std::vector<dyld_cache_image_clients_v2>        patchClients;
    __block std::vector<dyld_cache_patchable_export_v2>     clientExports;
    __block std::vector<dyld_cache_patchable_location_v2>   patchLocations;
    __block std::vector<char>                               patchExportNames;

    patchImages.reserve(numPatchImages);
    imageExports.reserve(numImageExports);
    patchClients.reserve(numPatchClients);
    clientExports.reserve(numClientExports);
    patchLocations.reserve(numPatchLocations);
    patchExportNames.reserve(numPatchExportNameBytes);

    // And now fill it with the patch data
    dyldCache->forEachImage(^(const mach_header* mh, const char* installName) {
        const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)mh;

        // On a customer cache, only store patch locations for interposable dylibs and weak binding
        bool dylibNeedsPatching = dyldCache->isOverridablePath(installName);

        DylibSymbolClients& dylibSymbolClients = _dylibToItsClients[ma];

        // Add the patch image which points in to the clients
        // Note we always add 1 patch image for every dylib in the cache, even if
        // it has no other data
        dyld_cache_image_patches_v2 patchImage;
        patchImage.patchClientsStartIndex   = (uint32_t)patchClients.size();
        patchImage.patchClientsCount        = 0;
        patchImage.patchExportsStartIndex   = (uint32_t)imageExports.size();
        patchImage.patchExportsCount        = (uint32_t)dylibSymbolClients._usedExports.size();

        for (auto& clientDylibAndUses : dylibSymbolClients._clientToUses) {
            bool clientUsed = false;

            uint64_t clientDylibBaseAddress = ((const dyld3::MachOAnalyzer*)clientDylibAndUses.first)->preferredLoadAddress();

            // We might add a client.  If we do, then set it up now so that we have the
            // right offset to the exports table
            dyld_cache_image_clients_v2 clientImage;
            clientImage.clientDylibIndex         = loaderToIndexMap.at(clientDylibAndUses.first->installName());
            clientImage.patchExportsStartIndex   = (uint32_t)clientExports.size();
            clientImage.patchExportsCount        = 0;

            for (auto& targetCacheOffsetAndUses : clientDylibAndUses.second._uses) {
                const CacheOffset& exportCacheOffset = targetCacheOffsetAndUses.first;
                if (!needsPatch(dylibNeedsPatching, ma, exportCacheOffset))
                    continue;

                std::vector<DylibSymbolClients::dyld_cache_patchable_location>& uses = targetCacheOffsetAndUses.second;
                if ( uses.empty() )
                    continue;

                // We have uses in this client->location->uses list.  Track them
                clientUsed = true;

                // We should have an export already, from the previous scan to size the tables
                auto exportIt = dylibSymbolClients._usedExports.find(exportCacheOffset);
                assert(exportIt != dylibSymbolClients._usedExports.end());

                uint32_t imageExportIndex = (uint32_t)std::distance(dylibSymbolClients._usedExports.begin(), exportIt);

                // Add an export for this client dylib
                dyld_cache_patchable_export_v2 cacheExport;
                cacheExport.imageExportIndex            = patchImage.patchExportsStartIndex + imageExportIndex;
                cacheExport.patchLocationsStartIndex    = (uint32_t)patchLocations.size();
                cacheExport.patchLocationsCount         = (uint32_t)uses.size();
                clientExports.push_back(cacheExport);
                ++clientImage.patchExportsCount;

                // Now add the list of locations.
                // At this point we need to translate from the locations the cache recorded to what we encode
                for (const DylibSymbolClients::dyld_cache_patchable_location& use : uses) {
                    dyld_cache_patchable_location_v2 loc;
                    loc.dylibOffsetOfUse            = (uint32_t)((_archLayout->sharedMemoryStart + use.cacheOffset) - clientDylibBaseAddress);
                    loc.high7                       = use.high7;
                    loc.addend                      = use.addend;
                    loc.authenticated               = use.authenticated;
                    loc.usesAddressDiversity        = use.usesAddressDiversity;
                    loc.key                         = use.key;
                    loc.discriminator               = use.discriminator;
                    patchLocations.push_back(loc);
                }
            }

            // Add the client to the table, if its used
            if ( clientUsed ) {
                ++patchImage.patchClientsCount;
                patchClients.push_back(clientImage);
            }
        }

        uint64_t imageBaseAddress = ma->preferredLoadAddress();

        // Add all the exports for this image
        for (const CacheOffset& exportCacheOffset : dylibSymbolClients._usedExports) {
            // Add the name, if no-one else has
            uint32_t exportNameOffset = 0;
            auto nameItAndInserted = exportNameOffsets.insert({ exportCacheOffset, (uint32_t)patchExportNames.size() });
            if ( nameItAndInserted.second ) {
                // We inserted the name, so make space for it
                std::string exportName = _exportsToName[exportCacheOffset];
                patchExportNames.insert(patchExportNames.end(), &exportName[0], &exportName[0] + exportName.size() + 1);
                exportNameOffset = nameItAndInserted.first->second;
            } else {
                // The name already existed.  Use the offset from earlier
                exportNameOffset = nameItAndInserted.first->second;
            }

            dyld_cache_image_export_v2 imageExport;
            imageExport.dylibOffsetOfImpl   = (uint32_t)((_archLayout->sharedMemoryStart + exportCacheOffset) - imageBaseAddress);
            imageExport.exportNameOffset    = (uint32_t)exportNameOffset;
            imageExports.push_back(imageExport);
        }

        patchImages.push_back(patchImage);
    });

    while ( (patchExportNames.size() % 4) != 0 )
        patchExportNames.push_back('\0');

    uint64_t patchInfoSize = sizeof(dyld_cache_patch_info_v2);
    patchInfoSize += sizeof(dyld_cache_image_patches_v2) * patchImages.size();
    patchInfoSize += sizeof(dyld_cache_image_export_v2) * imageExports.size();
    patchInfoSize += sizeof(dyld_cache_image_clients_v2) * patchClients.size();
    patchInfoSize += sizeof(dyld_cache_patchable_export_v2) * clientExports.size();
    patchInfoSize += sizeof(dyld_cache_patchable_location_v2) * patchLocations.size();
    patchInfoSize += patchExportNames.size();

    Region& readOnlyRegion = getSharedCacheReadOnlyRegion();

    // check for fit
    size_t freeSpace = readOnlyRegion.bufferSize - readOnlyRegion.sizeInUse;
    if ( patchInfoSize > freeSpace ) {
        _diagnostics.error("cache buffer too small to hold Trie (buffer size=%lldMB, patch size=%lluKB, free space=%ldMB)",
                            _allocatedBufferSize/1024/1024, patchInfoSize/1024, freeSpace/1024/1024);
        return;
    }

    // copy patch info into cache and update header
    dyldCache->header.patchInfoAddr = readOnlyRegion.unslidLoadAddress + readOnlyRegion.sizeInUse;
    dyldCache->header.patchInfoSize = patchInfoSize;

    dyld_cache_patch_info_v2 patchInfo;
    patchInfo.patchTableVersion             = 2;
    patchInfo.patchLocationVersion          = 0;
    patchInfo.patchTableArrayAddr           = dyldCache->header.patchInfoAddr + sizeof(dyld_cache_patch_info_v2);
    patchInfo.patchTableArrayCount          = patchImages.size();
    patchInfo.patchImageExportsArrayAddr    = patchInfo.patchTableArrayAddr + (patchInfo.patchTableArrayCount * sizeof(dyld_cache_image_patches_v2));
    patchInfo.patchImageExportsArrayCount   = imageExports.size();
    patchInfo.patchClientsArrayAddr         = patchInfo.patchImageExportsArrayAddr + (patchInfo.patchImageExportsArrayCount * sizeof(dyld_cache_image_export_v2));
    patchInfo.patchClientsArrayCount        = patchClients.size();
    patchInfo.patchClientExportsArrayAddr   = patchInfo.patchClientsArrayAddr + (patchInfo.patchClientsArrayCount * sizeof(dyld_cache_image_clients_v2));
    patchInfo.patchClientExportsArrayCount  = clientExports.size();
    patchInfo.patchLocationArrayAddr        = patchInfo.patchClientExportsArrayAddr + (patchInfo.patchClientExportsArrayCount * sizeof(dyld_cache_patchable_export_v2));
    patchInfo.patchLocationArrayCount       = patchLocations.size();
    patchInfo.patchExportNamesAddr          = patchInfo.patchLocationArrayAddr + (patchInfo.patchLocationArrayCount * sizeof(dyld_cache_patchable_location_v2));
    patchInfo.patchExportNamesSize          = patchExportNames.size();

    ::memcpy(readOnlyRegion.buffer + dyldCache->header.patchInfoAddr - readOnlyRegion.unslidLoadAddress,
             &patchInfo, sizeof(dyld_cache_patch_info_v2));
    ::memcpy(readOnlyRegion.buffer + patchInfo.patchTableArrayAddr - readOnlyRegion.unslidLoadAddress,
             &patchImages[0], sizeof(patchImages[0]) * patchImages.size());
    ::memcpy(readOnlyRegion.buffer + patchInfo.patchImageExportsArrayAddr - readOnlyRegion.unslidLoadAddress,
             &imageExports[0], sizeof(imageExports[0]) * imageExports.size());
    ::memcpy(readOnlyRegion.buffer + patchInfo.patchClientsArrayAddr - readOnlyRegion.unslidLoadAddress,
             &patchClients[0], sizeof(patchClients[0]) * patchClients.size());
    ::memcpy(readOnlyRegion.buffer + patchInfo.patchClientExportsArrayAddr - readOnlyRegion.unslidLoadAddress,
             &clientExports[0], sizeof(clientExports[0]) * clientExports.size());
    ::memcpy(readOnlyRegion.buffer + patchInfo.patchLocationArrayAddr - readOnlyRegion.unslidLoadAddress,
             &patchLocations[0], sizeof(patchLocations[0]) * patchLocations.size());
    ::memcpy(readOnlyRegion.buffer + patchInfo.patchExportNamesAddr - readOnlyRegion.unslidLoadAddress,
             &patchExportNames[0], patchExportNames.size());
    readOnlyRegion.sizeInUse += patchInfoSize;
}


void SharedCacheBuilder::buildLaunchSets(const std::vector<LoadedMachO>& osExecutables, const std::vector<LoadedMachO>& otherDylibs, const std::vector<LoadedMachO>& moreOtherDylibs)
{
    // build map for osDelegate to use for fileExists() and withReadOnlyMappedFile()
    const bool verbose = false;
    SyscallDelegate::PathToMapping otherMapping;
    for (const LoadedMachO& other : otherDylibs) {
        if (verbose) fprintf(stderr, "other: %s\n", other.inputFile->path);
        otherMapping[other.inputFile->path] = { other.mappedFile.mh, other.mappedFile.length };
    }
    for (const LoadedMachO& other : moreOtherDylibs) {
        if (verbose) fprintf(stderr, "more other: %s\n", other.inputFile->path);
        otherMapping[other.inputFile->path] = { other.mappedFile.mh, other.mappedFile.length };
    }


    // build PrebuiltLoaderSet for each executable and place in map<>
    DyldSharedCache* dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;
    std::map<std::string, const PrebuiltLoaderSet*> prebuiltsMap;
    for (const LoadedMachO& exe : osExecutables) {
        if (verbose) printf("osExecutable:  %s\n", exe.inputFile->path);
        // don't build PrebuiltLoaderSet for staged apps, as they will be run from a different location
        if ( strstr(exe.inputFile->path, "/staged_system_apps/") != nullptr )
            continue;
        const MachOAnalyzer*  mainMA = (MachOAnalyzer*)exe.loadedFileInfo.fileContent;
        KernelArgs            kernArgs(mainMA, {"test.exe"}, {}, {});
        SyscallDelegate       osDelegate;
        osDelegate._mappedOtherDylibs = otherMapping;
        osDelegate._gradedArchs       = _options.archs;
        osDelegate._dyldCache         = dyldCache;
        ProcessConfig         config(&kernArgs, osDelegate);
        RuntimeState          state(config);
        RuntimeState*         statePtr = &state;
        Diagnostics           launchDiag;

        config.reset(mainMA, exe.inputFile->path, dyldCache);
        state.resetCachedDylibsArrays();

        Loader::LoadOptions::Finder loaderFinder = ^(Diagnostics& diag, dyld3::Platform plat, const char* loadPath, const dyld4::Loader::LoadOptions& options) {
            uint32_t dylibIndex;
            // when building macOS cache, there may be some incorrect catalyst paths
            if ( (plat == dyld3::Platform::iOSMac) && (strncmp(loadPath, "/System/iOSSupport/", 19) != 0) ) {
                char altPath[PATH_MAX];
                strlcpy(altPath, "/System/iOSSupport", PATH_MAX);
                strlcat(altPath, loadPath, PATH_MAX);
                if ( dyldCache->hasImagePath(altPath, dylibIndex) ) {
                    const PrebuiltLoader* ldr = this->_cachedDylibsLoaderSet->atIndex(dylibIndex);
                    return (const Loader*)ldr;
                }
            }
            // first check if path is a dylib in the dyld cache, then use its PrebuiltLoader
            if ( dyldCache->hasImagePath(loadPath, dylibIndex) ) {
                const PrebuiltLoader* ldr = this->_cachedDylibsLoaderSet->atIndex(dylibIndex);
                return (const Loader*)ldr;
            }
            // call through to getLoader() which will expand @paths
            const Loader* ldr = Loader::getLoader(diag, *statePtr, loadPath, options);
            return (const Loader*)ldr;
        };


       if ( Loader* mainLoader = JustInTimeLoader::makeLaunchLoader(launchDiag, state, mainMA, exe.inputFile->path) ) {
            __block dyld4::MissingPaths missingPaths;
            auto missingLogger = ^(const char* mustBeMissingPath) {
                missingPaths.addPath(mustBeMissingPath);
            };
            Loader::LoadChain   loadChainMain { nullptr, mainLoader };
            Loader::LoadOptions options;
            options.staticLinkage   = true;
            options.launching       = true;
            options.canBeDylib      = true;
            options.rpathStack      = &loadChainMain;
            options.finder          = loaderFinder;
            options.pathNotFoundHandler = missingLogger;
            mainLoader->loadDependents(launchDiag, state, options);
            if ( launchDiag.hasError() ) {
                fprintf(stderr, "warning: can't build PrebuiltLoader for '%s': %s\n", exe.inputFile->path, launchDiag.errorMessageCStr());
                if (verbose) printf("skip  %s\n", exe.inputFile->path);
                continue;
            }
            state.setMainLoader(mainLoader);
            const PrebuiltLoaderSet* prebuiltAppSet = PrebuiltLoaderSet::makeLaunchSet(launchDiag, state, missingPaths);
            if ( launchDiag.hasError() ) {
                fprintf(stderr, "warning: can't build PrebuiltLoaderSet for '%s': %s\n", exe.inputFile->path, launchDiag.errorMessageCStr());
                if (verbose) printf("skip  %s\n", exe.inputFile->path);
                continue;
            }
            if ( prebuiltAppSet != nullptr ) {
                prebuiltsMap[exe.inputFile->path] = prebuiltAppSet;
                if (verbose) printf("%5lu %s\n", prebuiltAppSet->size(), exe.inputFile->path);
                state.setProcessPrebuiltLoaderSet(prebuiltAppSet);
                //prebuiltAppSet->print(state, stderr);
            }
        }
        else {
            fprintf(stderr, "warning: can't build PrebuiltLoaderSet for '%s': %s\n", exe.inputFile->path, launchDiag.errorMessageCStr());
        }
        // reclear byte array so that final cache created has them all zeroed
        state.resetCachedDylibsArrays();
    }

    Region& readOnlyRegion = getSharedCacheReadOnlyRegion();

    // copy all PrebuiltLoaderSets into cache
    size_t prebuiltsSpace = 0;
    for (const auto& entry : prebuiltsMap) {
        prebuiltsSpace += align(entry.second->size(),3);
    }
    readOnlyRegion.sizeInUse = align(readOnlyRegion.sizeInUse, 3);
    size_t freeSpace = readOnlyRegion.bufferSize - readOnlyRegion.sizeInUse;
    if ( prebuiltsSpace > freeSpace ) {
        _diagnostics.error("cache buffer too small to hold all PrebuiltLoaderSets (buffer size=%lldMB, PrebuiltLoaderSets size=%ldMB, free space=%ldMB)",
                            _allocatedBufferSize/1024/1024, prebuiltsSpace/1024/1024, freeSpace/1024/1024);
        return;
    }
    dyldCache->header.programsPBLSetPoolAddr = readOnlyRegion.unslidLoadAddress + readOnlyRegion.sizeInUse;
    uint8_t* poolBase = readOnlyRegion.buffer + readOnlyRegion.sizeInUse;
    __block std::vector<DylibIndexTrie::Entry> trieEntrys;
    uint32_t currentPoolOffset = 0;
    for (const auto& entry : prebuiltsMap) {
        const PrebuiltLoaderSet* pbls = entry.second;
        trieEntrys.push_back(DylibIndexTrie::Entry(entry.first, DylibIndex(currentPoolOffset)));

        // Add cdHashes to the trie so that we can look up by cdHash at runtime
        // Assumes that cdHash strings at runtime use lowercase a-f digits
        const PrebuiltLoader* mainPbl = pbls->atIndex(0);
        mainPbl->withCDHash(^(const uint8_t *cdHash) {
            std::string cdHashStr = "/cdhash/";
            cdHashStr.reserve(24);
            for (int i=0; i < 20; ++i) {
                uint8_t byte = cdHash[i];
                uint8_t nibbleL = byte & 0x0F;
                uint8_t nibbleH = byte >> 4;
                if ( nibbleH < 10 )
                    cdHashStr += '0' + nibbleH;
                else
                    cdHashStr += 'a' + (nibbleH-10);
                if ( nibbleL < 10 )
                    cdHashStr += '0' + nibbleL;
                else
                    cdHashStr += 'a' + (nibbleL-10);
            }
            trieEntrys.push_back(DylibIndexTrie::Entry(cdHashStr, DylibIndex(currentPoolOffset)));
        });

        size_t size = pbls->size();
        ::memcpy(poolBase+currentPoolOffset, pbls, size);
        currentPoolOffset += align(size,3);
        freeSpace -= size;
        pbls->deallocate();
    }
    dyldCache->header.programsPBLSetPoolSize = currentPoolOffset;
    readOnlyRegion.sizeInUse += currentPoolOffset;
    freeSpace = readOnlyRegion.bufferSize - readOnlyRegion.sizeInUse;
    // build trie of indexes into closures list
    DylibIndexTrie programTrie(trieEntrys);
    std::vector<uint8_t> trieBytes;
    programTrie.emit(trieBytes);
    while ( (trieBytes.size() % 8) != 0 )
        trieBytes.push_back(0);
    if ( trieBytes.size() > freeSpace ) {
        _diagnostics.error("cache buffer too small to hold PrebuiltLoaderSet trie (buffer size=%lldMB, trie size=%ldMB, free space=%ldMB)",
                            _allocatedBufferSize/1024/1024, trieBytes.size()/1024/1024, freeSpace/1024/1024);
        return;
    }
    ::memcpy(readOnlyRegion.buffer + readOnlyRegion.sizeInUse, &trieBytes[0], trieBytes.size());
    dyldCache->header.programTrieAddr = readOnlyRegion.unslidLoadAddress + readOnlyRegion.sizeInUse;
    dyldCache->header.programTrieSize = (uint32_t)trieBytes.size();
    readOnlyRegion.sizeInUse += trieBytes.size();
    readOnlyRegion.sizeInUse = align(readOnlyRegion.sizeInUse, 14);
}

#if 0
void SharedCacheBuilder::emitContantObjects() {
    if ( _coalescedText.cfStrings.bufferSize == 0 )
        return;

    Region& firstReadExecuteRegion = _subCaches.front()._readExecuteRegion;

    assert(_coalescedText.cfStrings.isaInstallName != nullptr);
    DyldSharedCache* cache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;
    __block uint64_t targetSymbolOffsetInCache = 0;
    __block const dyld3::MachOAnalyzer* targetSymbolMA = nullptr;
    __block const dyld3::MachOAnalyzer* libdyldMA = nullptr;
    cache->forEachImage(^(const mach_header* mh, const char* installName) {
        const dyld3::MachOAnalyzer* ma = (const dyld3::MachOAnalyzer*)mh;

        if ( strcmp(installName, "/usr/lib/system/libdyld.dylib") == 0 ) {
            libdyldMA = ma;
        }

        if ( targetSymbolOffsetInCache != 0 )
            return;
        if ( strcmp(installName, _coalescedText.cfStrings.isaInstallName) != 0 )
            return;
        dyld3::MachOAnalyzer::FoundSymbol foundInfo;
        bool foundSymbol = ma->findExportedSymbol(_diagnostics, _coalescedText.cfStrings.isaClassName,
                                                  false, foundInfo, nullptr);
        if ( foundSymbol ) {
            targetSymbolOffsetInCache = (uint8_t*)ma - firstReadExecuteRegion.buffer + foundInfo.value;
            targetSymbolMA = ma;
        }
    });
    if ( targetSymbolOffsetInCache == 0 ) {
        _diagnostics.error("Could not find export of '%s' in '%s'", _coalescedText.cfStrings.isaClassName,
                           _coalescedText.cfStrings.isaInstallName);
        return;
    }
    if ( libdyldMA == nullptr ) {
        _diagnostics.error("Could not libdyld.dylib in shared cache");
        return;
    }

    // If all binds to this symbol were via CF constants, then we'll never have seen the ISA patch export
    // os add it now just in case
    _dylibToItsExports[targetSymbolMA].insert(targetSymbolOffsetInCache);
    _exportsToName[targetSymbolOffsetInCache] = _coalescedText.cfStrings.isaClassName;

    // The subCache containing libobjc should get CFString's for now
    CacheBuilder::ASLR_Tracker* objcASLRTracker = &_objcReadWriteMetadataSubCache->_aslrTracker;

    // CFString's have so far just been memcpy'ed from the source dylib to the shared cache.
    // We now need to rewrite their ISAs to be rebases to the ___CFConstantStringClassReference class
    const uint64_t cfStringAtomSize = (uint64_t)DyldSharedCache::ConstantClasses::cfStringAtomSize;
    assert( (_coalescedText.cfStrings.bufferSize % cfStringAtomSize) == 0);
    for (uint64_t bufferOffset = 0; bufferOffset != _coalescedText.cfStrings.bufferSize; bufferOffset += cfStringAtomSize) {
        uint8_t* atomBuffer = _coalescedText.cfStrings.bufferAddr + bufferOffset;
        // The ISA fixup is at an offset of 0 in to the atom
        uint8_t* fixupLoc = atomBuffer;
        // We purposefully want to remove the pointer authentication from the ISA so
        // just use an empty pointer metadata
        dyld3::MachOLoaded::PointerMetaData pmd;
        uint64_t addend = 0;
        _exportsToUses[targetSymbolOffsetInCache].push_back(makePatchLocation(fixupLoc - firstReadExecuteRegion.buffer, pmd, addend));
        *(uint64_t*)fixupLoc = _archLayout->sharedMemoryStart + targetSymbolOffsetInCache;
        objcASLRTracker->add(fixupLoc);
    }

    // Set the ranges in the libdyld in the shared cache.  At runtime we can use these to quickly check if a given address
    // is a valid constant
    typedef std::pair<const uint8_t*, const uint8_t*> ObjCConstantRange;
    std::pair<const void*, uint64_t> sharedCacheRanges = cache->getObjCConstantRange();
    uint64_t numRanges = sharedCacheRanges.second / sizeof(ObjCConstantRange);
    dyld3::Array<ObjCConstantRange> rangeArray((ObjCConstantRange*)sharedCacheRanges.first, numRanges, numRanges);

    // The subCache containing libdyld should contain the ranges metadata
    CacheBuilder::ASLR_Tracker* libdyldASLRTracker = &_libdyldReadWriteMetadataSubCache->_aslrTracker;

    if ( numRanges > dyld_objc_string_kind ) {
        rangeArray[dyld_objc_string_kind].first = (const uint8_t*)_coalescedText.cfStrings.bufferVMAddr;
        rangeArray[dyld_objc_string_kind].second = rangeArray[dyld_objc_string_kind].first + _coalescedText.cfStrings.bufferSize;
        libdyldASLRTracker->add(&rangeArray[dyld_objc_string_kind].first);
        libdyldASLRTracker->add(&rangeArray[dyld_objc_string_kind].second);
    }

    // Update the __SHARED_CACHE range in libdyld to contain the cf/objc constants
    libdyldMA->forEachLoadCommand(_diagnostics, ^(const load_command* cmd, bool& stop) {
        // We don't handle 32-bit as this is only needed for pointer authentication
        assert(cmd->cmd != LC_SEGMENT);
        if ( cmd->cmd == LC_SEGMENT_64 ) {
            segment_command_64* seg = (segment_command_64*)cmd;
            if ( strcmp(seg->segname, "__SHARED_CACHE") == 0 ) {
                // Update the range of this segment, and any sections inside
                seg->vmaddr     = _coalescedText.cfStrings.bufferVMAddr;
                seg->vmsize     = _coalescedText.cfStrings.bufferSize;
                seg->fileoff    = _coalescedText.cfStrings.cacheFileOffset;
                seg->fileoff    = _coalescedText.cfStrings.bufferSize;
                section_64* const sectionsStart = (section_64*)((char*)seg + sizeof(struct segment_command_64));
                section_64* const sectionsEnd   = &sectionsStart[seg->nsects];
                for (section_64* sect=sectionsStart; sect < sectionsEnd; ++sect) {
                    if ( !strcmp(sect->sectname, "__cfstring") ) {
                        sect->addr      = _coalescedText.cfStrings.bufferVMAddr;
                        sect->size      = _coalescedText.cfStrings.bufferSize;
                        sect->offset    = (uint32_t)_coalescedText.cfStrings.cacheFileOffset;
                    }
                }
                stop = true;
            }
        }
    });
}
#endif


bool SharedCacheBuilder::writeSubCache(const SubCache& subCache, void (^cacheSizeCallback)(uint64_t size), bool (^copyCallback)(const uint8_t* src, uint64_t size, uint64_t dstOffset))
{
    const dyld_cache_header*       cacheHeader = (dyld_cache_header*)subCache._readExecuteRegion.buffer;
    const dyld_cache_mapping_info* mappings = (dyld_cache_mapping_info*)(subCache._readExecuteRegion.buffer + cacheHeader->mappingOffset);
    const uint32_t mappingsCount = cacheHeader->mappingCount;
    // Check the sizes of all the regions are correct
    assert(subCache._readExecuteRegion.sizeInUse       == mappings[0].size);
    for (uint32_t i = 0; i != subCache._dataRegions.size(); ++i) {
        assert(subCache._dataRegions[i].sizeInUse == mappings[i + 1].size);
    }
    if ( subCache._readOnlyRegion.has_value() )
        assert(subCache._readOnlyRegion->sizeInUse == mappings[mappingsCount - 1].size);

    // Check the file offsets of all the regions are correct
    assert(subCache._readExecuteRegion.cacheFileOffset == mappings[0].fileOffset);
    for (uint32_t i = 0; i != subCache._dataRegions.size(); ++i) {
        assert(subCache._dataRegions[i].cacheFileOffset   == mappings[i + 1].fileOffset);
    }
    if ( subCache._readOnlyRegion.has_value() )
        assert(subCache._readOnlyRegion->cacheFileOffset == mappings[mappingsCount - 1].fileOffset);
    assert(subCache._codeSignatureRegion.sizeInUse     == cacheHeader->codeSignatureSize);
    if ( &subCache == &_localSymbolsSubCache ) {
        assert(cacheHeader->codeSignatureOffset == (subCache.highestFileOffset() + _localSymbolsRegion.sizeInUse) );
    } else {
        assert(cacheHeader->codeSignatureOffset == subCache.highestFileOffset() );
    }

    // Make sure the slidable mappings have the same ranges as the original mappings
    const dyld_cache_mapping_and_slide_info* slidableMappings = (dyld_cache_mapping_and_slide_info*)(subCache._readExecuteRegion.buffer + cacheHeader->mappingWithSlideOffset);
    assert(cacheHeader->mappingCount == cacheHeader->mappingWithSlideCount);
    for (uint32_t i = 0; i != cacheHeader->mappingCount; ++i) {
        assert(mappings[i].address      == slidableMappings[i].address);
        assert(mappings[i].size         == slidableMappings[i].size);
        assert(mappings[i].fileOffset   == slidableMappings[i].fileOffset);
        assert(mappings[i].maxProt      == slidableMappings[i].maxProt);
        assert(mappings[i].initProt     == slidableMappings[i].initProt);
    }

    // Now that we know everything is correct, actually copy the data
    {
        uint64_t sizeInUse = 0;
        sizeInUse += subCache._readExecuteRegion.sizeInUse;
        sizeInUse += subCache.dataRegionsSizeInUse();
        sizeInUse += subCache._readOnlyRegion.has_value() ? subCache._readOnlyRegion->sizeInUse : 0;
        sizeInUse += subCache._codeSignatureRegion.sizeInUse;
        if ( &subCache == &_localSymbolsSubCache )
            sizeInUse += _localSymbolsRegion.sizeInUse;
        cacheSizeCallback(sizeInUse);
    }
    bool fullyWritten = copyCallback(subCache._readExecuteRegion.buffer, subCache._readExecuteRegion.sizeInUse, mappings[0].fileOffset);
    for (uint32_t i = 0; i != subCache._dataRegions.size(); ++i) {
        fullyWritten &= copyCallback(subCache._dataRegions[i].buffer, subCache._dataRegions[i].sizeInUse, mappings[i + 1].fileOffset);
    }
    if ( subCache._readOnlyRegion.has_value() ) {
        const Region& readOnlyRegion = *(subCache._readOnlyRegion);
        fullyWritten &= copyCallback(readOnlyRegion.buffer, readOnlyRegion.sizeInUse, mappings[cacheHeader->mappingCount - 1].fileOffset);
    }
    if ( (_localSymbolsRegion.sizeInUse != 0) && (&subCache == &_localSymbolsSubCache) ) {
        // The locals cache has only __TEXT
        assert(cacheHeader->mappingCount == 1);
        assert(cacheHeader->localSymbolsOffset == mappings[0].fileOffset+subCache._readExecuteRegion.sizeInUse);
        fullyWritten &= copyCallback(_localSymbolsRegion.buffer, _localSymbolsRegion.sizeInUse, cacheHeader->localSymbolsOffset);
    }
    fullyWritten &= copyCallback(subCache._codeSignatureRegion.buffer, subCache._codeSignatureRegion.sizeInUse, cacheHeader->codeSignatureOffset);
    return fullyWritten;
}


void SharedCacheBuilder::writeSubCacheFile(const SubCache& subCache, const std::string& path)
{
    std::string pathTemplate = path + "-XXXXXX";
    size_t templateLen = strlen(pathTemplate.c_str())+2;
    BLOCK_ACCCESSIBLE_ARRAY(char, pathTemplateSpace, templateLen);
    strlcpy(pathTemplateSpace, pathTemplate.c_str(), templateLen);
    int fd = mkstemp(pathTemplateSpace);
    if ( fd != -1 ) {
        auto cacheSizeCallback = ^(uint64_t size) {
            // set final cache file size (may help defragment file)
            ::ftruncate(fd, size);
        };
        auto copyCallback = ^(const uint8_t* src, uint64_t size, uint64_t dstOffset) {
            uint64_t writtenSize = pwrite(fd, src, size, dstOffset);
            return writtenSize == size;
        };
        // <rdar://problem/55370916> TOCTOU: verify path is still a realpath (not changed)
        char tempPath[MAXPATHLEN];
        if ( ::fcntl(fd, F_GETPATH, tempPath) == 0 ) {
            size_t tempPathLen = strlen(tempPath);
            if ( tempPathLen > 7 )
                tempPath[tempPathLen-7] = '\0'; // remove trailing -xxxxxx
            if ( path != tempPath ) {
                _diagnostics.error("output file path changed from: '%s' to: '%s'", path.c_str(), tempPath);
                ::close(fd);
                return;
            }
        }
        else {
            _diagnostics.error("unable to fcntl(fd, F_GETPATH) on output file");
            ::close(fd);
            return;
        }
        bool fullyWritten = writeSubCache(subCache, cacheSizeCallback, copyCallback);
        if ( fullyWritten ) {
            ::fchmod(fd, S_IRUSR|S_IRGRP|S_IROTH); // mkstemp() makes file "rw-------", switch it to "r--r--r--"
            // <rdar://problem/55370916> TOCTOU: verify path is still a realpath (not changed)
            // For MRM bringup, dyld installs symlinks from:
            //   dyld_shared_cache_x86_64 -> ../../../../System/Library/dyld/dyld_shared_cache_x86_64
            //   dyld_shared_cache_x86_64h -> ../../../../System/Library/dyld/dyld_shared_cache_x86_64h
            // We don't want to follow that symlink when we install the cache, but instead write over it
            auto lastSlash = path.find_last_of("/");
            if ( lastSlash != std::string::npos ) {
                std::string directoryPath = path.substr(0, lastSlash);

                char resolvedPath[PATH_MAX];
                ::realpath(directoryPath.c_str(), resolvedPath);
                // Note: if the target cache file does not already exist, realpath() will return NULL, but still fill in the path buffer
                if ( directoryPath != resolvedPath ) {
                    _diagnostics.error("output directory file path changed from: '%s' to: '%s'", directoryPath.c_str(), resolvedPath);
                    return;
                }
            }
            if ( ::rename(pathTemplateSpace, path.c_str()) == 0) {
                ::close(fd);
                return; // success
            } else {
                _diagnostics.error("could not rename file '%s' to: '%s'", pathTemplateSpace, path.c_str());
            }
        }
        else {
            _diagnostics.error("could not write file %s", pathTemplateSpace);
        }
        ::close(fd);
        ::unlink(pathTemplateSpace);
    }
    else {
        _diagnostics.error("could not open file %s", pathTemplateSpace);
    }
}

void SharedCacheBuilder::writeFile(const std::string& path)
{
    std::string suffix = "";
    uint32_t index = 0;
    for (const SubCache& subCache : _subCaches) {
        writeSubCacheFile(subCache, path + suffix);
        ++index;
        suffix = std::string(".") + dyld3::json::decimal(index);
    }
}

void SharedCacheBuilder::writeBuffers(std::vector<CacheBuffer>& cacheBuffers) {
    for (const SubCache& subCache : _subCaches) {
        __block uint8_t* buffer = nullptr;
        __block uint64_t bufferSize = 0;
        auto cacheSizeCallback = ^(uint64_t size) {
            buffer = (uint8_t*)malloc(size);
            bufferSize = size;
        };
        auto copyCallback = ^(const uint8_t* src, uint64_t size, uint64_t dstOffset) {
            memcpy(buffer + dstOffset, src, size);
            return true;
        };

        bool fullyWritten = writeSubCache(subCache, cacheSizeCallback, copyCallback);
        assert(fullyWritten);

        CacheBuffer cacheBuffer;
        cacheBuffer.bufferData = buffer;
        cacheBuffer.bufferSize = bufferSize;
        cacheBuffer.cdHash = subCache.cdHashFirst();
        cacheBuffer.uuid = subCache.uuid();
        cacheBuffers.push_back(cacheBuffer);
    }
}

void SharedCacheBuilder::writeSymbolFileBuffer(CacheBuffer& cacheBuffer) {
    if ( _localSymbolsRegion.sizeInUse == 0 )
        return;

    __block uint8_t* buffer = nullptr;
    __block uint64_t bufferSize = 0;
    auto cacheSizeCallback = ^(uint64_t size) {
        buffer = (uint8_t*)malloc(size);
        bufferSize = size;
    };
    auto copyCallback = ^(const uint8_t* src, uint64_t size, uint64_t dstOffset) {
        memcpy(buffer + dstOffset, src, size);
        return true;
    };

    bool fullyWritten = writeSubCache(_localSymbolsSubCache, cacheSizeCallback, copyCallback);
    assert(fullyWritten);

    cacheBuffer.bufferData = buffer;
    cacheBuffer.bufferSize = bufferSize;
    cacheBuffer.cdHash = _localSymbolsSubCache.cdHashFirst();
    cacheBuffer.uuid = _localSymbolsSubCache.uuid();
}

void SharedCacheBuilder::writeMapFile(const std::string& path)
{
    std::string mapContent = getMapFileBuffer();
    safeSave(mapContent.c_str(), mapContent.size(), path);
}

std::string SharedCacheBuilder::getMapFileBuffer() const
{
    DyldSharedCache* cache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;
    return cache->mapFile();
}

std::string SharedCacheBuilder::getMapFileJSONBuffer(const std::string& cacheDisposition) const
{
    DyldSharedCache* cache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;
    return cache->generateJSONMap(cacheDisposition.c_str());
}

void SharedCacheBuilder::markPaddingInaccessible()
{
    for (const SubCache& subCache : _subCaches) {
        // region between RX and RW
        if ( const Region* dataRegion = subCache.firstDataRegion() ) {
            uint8_t* startPad1 = subCache._readExecuteRegion.buffer+subCache._readExecuteRegion.sizeInUse;
            uint8_t* endPad1   = dataRegion->buffer;
            ::vm_protect(mach_task_self(), (vm_address_t)startPad1, endPad1-startPad1, false, 0);
        }

        // region between RW and RO
        if ( const Region* lastRegion = subCache.lastDataRegion() ) {
            if ( subCache._readOnlyRegion.has_value() ) {
                uint8_t* startPad2 = lastRegion->buffer+lastRegion->sizeInUse;
                uint8_t* endPad2   = subCache._readOnlyRegion->buffer;
                ::vm_protect(mach_task_self(), (vm_address_t)startPad2, endPad2-startPad2, false, 0);
            }
        }
    }
}


void SharedCacheBuilder::forEachCacheDylib(void (^callback)(const std::string& path)) {
    for (const DylibInfo& dylibInfo : _sortedDylibs)
        callback(dylibInfo.dylibID);
}


void SharedCacheBuilder::forEachCacheSymlink(void (^callback)(const std::string& path))
{
    for (const std::string& aliasPath : _dylibAliases) {
        callback(aliasPath);
    }
}


uint64_t SharedCacheBuilder::pathHash(const char* path)
{
    uint64_t sum = 0;
    for (const char* s=path; *s != '\0'; ++s)
        sum += sum*4 + *s;
    return sum;
}


void SharedCacheBuilder::findDylibAndSegment(const void* contentPtr, std::string& foundDylibName, std::string& foundSegName)
{
    foundDylibName = "???";
    foundSegName   = "???";
    Region& firstReadExecuteRegion = _subCaches.front()._readExecuteRegion;
    uint64_t unslidVmAddr = ((uint8_t*)contentPtr - firstReadExecuteRegion.buffer) + firstReadExecuteRegion.unslidLoadAddress;
    DyldSharedCache* cache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;
    cache->forEachImage(^(const mach_header* mh, const char* installName) {
        ((dyld3::MachOLoaded*)mh)->forEachSegment(^(const dyld3::MachOFile::SegmentInfo& info, bool &stop) {
            if ( (unslidVmAddr >= info.vmAddr) && (unslidVmAddr < (info.vmAddr+info.vmSize)) ) {
                foundDylibName = installName;
                foundSegName   = info.segName;
                stop           = true;
            }
        });
    });
}


void SharedCacheBuilder::fipsSign()
{
    // find libcorecrypto.dylib in cache being built
    DyldSharedCache* dyldCache = (DyldSharedCache*)_subCaches.front()._readExecuteRegion.buffer;
    __block const dyld3::MachOLoaded* ml = nullptr;
    dyldCache->forEachImage(^(const mach_header* mh, const char* installName) {
        if ( strcmp(installName, "/usr/lib/system/libcorecrypto.dylib") == 0 )
            ml = (dyld3::MachOLoaded*)mh;
    });
    if ( ml == nullptr ) {
        _diagnostics.warning("Could not find libcorecrypto.dylib, skipping FIPS sealing");
        return;
    }

    // find location in libcorecrypto.dylib to store hash of __text section
    uint64_t hashStoreSize;
    const void* hashStoreLocation = ml->findSectionContent("__TEXT", "__fips_hmacs", hashStoreSize);
    if ( hashStoreLocation == nullptr ) {
        _diagnostics.warning("Could not find __TEXT/__fips_hmacs section in libcorecrypto.dylib, skipping FIPS sealing");
        return;
    }
    if ( hashStoreSize != 32 ) {
        _diagnostics.warning("__TEXT/__fips_hmacs section in libcorecrypto.dylib is not 32 bytes in size, skipping FIPS sealing");
        return;
    }

    // compute hmac hash of __text section
    uint64_t textSize;
    const void* textLocation = ml->findSectionContent("__TEXT", "__text", textSize);
    if ( textLocation == nullptr ) {
        _diagnostics.warning("Could not find __TEXT/__text section in libcorecrypto.dylib, skipping FIPS sealing");
        return;
    }
    unsigned char hmac_key = 0;
    CCHmac(kCCHmacAlgSHA256, &hmac_key, 1, textLocation, textSize, (void*)hashStoreLocation); // store hash directly into hashStoreLocation
}

void SharedCacheBuilder::codeSign(SubCache& subCache)
{
    uint8_t  dscHashType;
    uint8_t  dscHashSize;
    uint32_t dscDigestFormat;
    bool agile = false;

    // select which codesigning hash
    switch (_options.codeSigningDigestMode) {
        case DyldSharedCache::Agile:
            agile = true;
            // Fall through to SHA1, because the main code directory remains SHA1 for compatibility.
            [[clang::fallthrough]];
        case DyldSharedCache::SHA1only:
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            dscHashType     = CS_HASHTYPE_SHA1;
            dscHashSize     = CS_HASH_SIZE_SHA1;
            dscDigestFormat = kCCDigestSHA1;
#pragma clang diagnostic pop
            break;
        case DyldSharedCache::SHA256only:
            dscHashType     = CS_HASHTYPE_SHA256;
            dscHashSize     = CS_HASH_SIZE_SHA256;
            dscDigestFormat = kCCDigestSHA256;
            break;
        default:
            _diagnostics.error("codeSigningDigestMode has unknown, unexpected value %d, bailing out.",
                               _options.codeSigningDigestMode);
            return;
    }

    std::string cacheIdentifier = "com.apple.dyld.cache.";
    cacheIdentifier +=  _options.archs->name();
    if ( &subCache == &_localSymbolsSubCache ) {
        cacheIdentifier += ".symbols";
    } else if ( _options.dylibsRemovedDuringMastering ) {
        if ( _options.optimizeStubs  )
            cacheIdentifier +=  ".release";
        else
            cacheIdentifier += ".development";
    }

    // get pointers into shared cache buffer
    size_t inBbufferSize = 0;
    inBbufferSize += subCache._readExecuteRegion.sizeInUse;
    inBbufferSize += subCache.dataRegionsSizeInUse();
    inBbufferSize += subCache._readOnlyRegion.has_value() ? subCache._readOnlyRegion->sizeInUse : 0;
    if ( &subCache == &_localSymbolsSubCache )
        inBbufferSize += _localSymbolsRegion.sizeInUse;
    const uint16_t  pageSize      = _archLayout->csPageSize;

    // layout code signature contents
    uint32_t blobCount     = agile ? 4 : 3;
    size_t   idSize        = cacheIdentifier.size()+1; // +1 for terminating 0
    uint32_t slotCount     = (uint32_t)((inBbufferSize + pageSize - 1) / pageSize);
    uint32_t xSlotCount    = CSSLOT_REQUIREMENTS;
    size_t   idOffset      = offsetof(CS_CodeDirectory, end_withExecSeg);
    size_t   hashOffset    = idOffset+idSize + dscHashSize*xSlotCount;
    size_t   hash256Offset = idOffset+idSize + CS_HASH_SIZE_SHA256*xSlotCount;
    size_t   cdSize        = hashOffset + (slotCount * dscHashSize);
    size_t   cd256Size     = agile ? hash256Offset + (slotCount * CS_HASH_SIZE_SHA256) : 0;
    size_t   reqsSize      = 12;
    size_t   cmsSize       = sizeof(CS_Blob);
    size_t   cdOffset      = sizeof(CS_SuperBlob) + blobCount*sizeof(CS_BlobIndex);
    size_t   cd256Offset   = cdOffset + cdSize;
    size_t   reqsOffset    = cd256Offset + cd256Size; // equals cdOffset + cdSize if not agile
    size_t   cmsOffset     = reqsOffset + reqsSize;
    size_t   sbSize        = cmsOffset + cmsSize;
    size_t   sigSize       = align(sbSize, 14);       // keep whole cache 16KB aligned

    // allocate space for blob
    vm_address_t codeSigAlloc;
    if ( vm_allocate(mach_task_self(), &codeSigAlloc, sigSize, VM_FLAGS_ANYWHERE) != 0 ) {
        _diagnostics.error("could not allocate code signature buffer");
        return;
    }
    subCache._codeSignatureRegion.buffer     = (uint8_t*)codeSigAlloc;
    subCache._codeSignatureRegion.bufferSize = sigSize;
    subCache._codeSignatureRegion.sizeInUse  = sigSize;

    // create overall code signature which is a superblob
    CS_SuperBlob* sb = reinterpret_cast<CS_SuperBlob*>(subCache._codeSignatureRegion.buffer);
    sb->magic           = htonl(CSMAGIC_EMBEDDED_SIGNATURE);
    sb->length          = htonl(sbSize);
    sb->count           = htonl(blobCount);
    sb->index[0].type   = htonl(CSSLOT_CODEDIRECTORY);
    sb->index[0].offset = htonl(cdOffset);
    sb->index[1].type   = htonl(CSSLOT_REQUIREMENTS);
    sb->index[1].offset = htonl(reqsOffset);
    sb->index[2].type   = htonl(CSSLOT_CMS_SIGNATURE);
    sb->index[2].offset = htonl(cmsOffset);
    if ( agile ) {
        sb->index[3].type = htonl(CSSLOT_ALTERNATE_CODEDIRECTORIES + 0);
        sb->index[3].offset = htonl(cd256Offset);
    }

    // fill in empty requirements
    CS_RequirementsBlob* reqs = (CS_RequirementsBlob*)(((char*)sb)+reqsOffset);
    reqs->magic  = htonl(CSMAGIC_REQUIREMENTS);
    reqs->length = htonl(sizeof(CS_RequirementsBlob));
    reqs->data   = 0;

    // initialize fixed fields of Code Directory
    CS_CodeDirectory* cd = (CS_CodeDirectory*)(((char*)sb)+cdOffset);
    cd->magic           = htonl(CSMAGIC_CODEDIRECTORY);
    cd->length          = htonl(cdSize);
    cd->version         = htonl(0x20400);               // supports exec segment
    cd->flags           = htonl(kSecCodeSignatureAdhoc);
    cd->hashOffset      = htonl(hashOffset);
    cd->identOffset     = htonl(idOffset);
    cd->nSpecialSlots   = htonl(xSlotCount);
    cd->nCodeSlots      = htonl(slotCount);
    cd->codeLimit       = htonl(inBbufferSize);
    cd->hashSize        = dscHashSize;
    cd->hashType        = dscHashType;
    cd->platform        = 0;                            // not platform binary
    cd->pageSize        = __builtin_ctz(pageSize);      // log2(CS_PAGE_SIZE);
    cd->spare2          = 0;                            // unused (must be zero)
    cd->scatterOffset   = 0;                            // not supported anymore
    cd->teamOffset      = 0;                            // no team ID
    cd->spare3          = 0;                            // unused (must be zero)
    cd->codeLimit64     = 0;                            // falls back to codeLimit

    // executable segment info
    cd->execSegBase     = htonll(subCache._readExecuteRegion.cacheFileOffset); // base of TEXT segment
    cd->execSegLimit    = htonll(subCache._readExecuteRegion.sizeInUse);       // size of TEXT segment
    cd->execSegFlags    = 0;                                          // not a main binary

    // initialize dynamic fields of Code Directory
    strcpy((char*)cd + idOffset, cacheIdentifier.c_str());

    // add special slot hashes
    uint8_t* hashSlot = (uint8_t*)cd + hashOffset;
    uint8_t* reqsHashSlot = &hashSlot[-CSSLOT_REQUIREMENTS*dscHashSize];
    CCDigest(dscDigestFormat, (uint8_t*)reqs, sizeof(CS_RequirementsBlob), reqsHashSlot);

    CS_CodeDirectory* cd256;
    uint8_t* hash256Slot;
    uint8_t* reqsHash256Slot;
    if ( agile ) {
        // Note that the assumption here is that the size up to the hashes is the same as for
        // sha1 code directory, and that they come last, after everything else.

        cd256 = (CS_CodeDirectory*)(((char*)sb)+cd256Offset);
        cd256->magic           = htonl(CSMAGIC_CODEDIRECTORY);
        cd256->length          = htonl(cd256Size);
        cd256->version         = htonl(0x20400);               // supports exec segment
        cd256->flags           = htonl(kSecCodeSignatureAdhoc);
        cd256->hashOffset      = htonl(hash256Offset);
        cd256->identOffset     = htonl(idOffset);
        cd256->nSpecialSlots   = htonl(xSlotCount);
        cd256->nCodeSlots      = htonl(slotCount);
        cd256->codeLimit       = htonl(inBbufferSize);
        cd256->hashSize        = CS_HASH_SIZE_SHA256;
        cd256->hashType        = CS_HASHTYPE_SHA256;
        cd256->platform        = 0;                            // not platform binary
        cd256->pageSize        = __builtin_ctz(pageSize);      // log2(CS_PAGE_SIZE);
        cd256->spare2          = 0;                            // unused (must be zero)
        cd256->scatterOffset   = 0;                            // not supported anymore
        cd256->teamOffset      = 0;                            // no team ID
        cd256->spare3          = 0;                            // unused (must be zero)
        cd256->codeLimit64     = 0;                            // falls back to codeLimit

        // executable segment info
        cd256->execSegBase     = cd->execSegBase;
        cd256->execSegLimit    = cd->execSegLimit;
        cd256->execSegFlags    = cd->execSegFlags;

        // initialize dynamic fields of Code Directory
        strcpy((char*)cd256 + idOffset, cacheIdentifier.c_str());

        // add special slot hashes
        hash256Slot = (uint8_t*)cd256 + hash256Offset;
        reqsHash256Slot = &hash256Slot[-CSSLOT_REQUIREMENTS*CS_HASH_SIZE_SHA256];
        CCDigest(kCCDigestSHA256, (uint8_t*)reqs, sizeof(CS_RequirementsBlob), reqsHash256Slot);
    }
    else {
        cd256 = NULL;
        hash256Slot = NULL;
        reqsHash256Slot = NULL;
    }

    // fill in empty CMS blob for ad-hoc signing
    CS_Blob* cms = (CS_Blob*)(((char*)sb)+cmsOffset);
    cms->magic  = htonl(CSMAGIC_BLOBWRAPPER);
    cms->length = htonl(sizeof(CS_Blob));


    // alter header of cache to record size and location of code signature
    // do this *before* hashing each page
    dyld_cache_header* cache = (dyld_cache_header*)subCache._readExecuteRegion.buffer;
    cache->codeSignatureOffset  = inBbufferSize;
    cache->codeSignatureSize    = sigSize;

    struct SlotRange {
        uint64_t        start   = 0;
        uint64_t        end     = 0;
        const uint8_t*  buffer  = nullptr;
    };
    std::vector<SlotRange> regionSlots;
    // __TEXT
    regionSlots.push_back({ 0, (subCache._readExecuteRegion.sizeInUse / pageSize), subCache._readExecuteRegion.buffer });
    // __DATA
    for (const Region& dataRegion : subCache._dataRegions) {
        // The first data region starts at the end of __TEXT, and subsequent regions are
        // after the previous __DATA region.
        uint64_t previousEnd = regionSlots.back().end;
        uint64_t numSlots = dataRegion.sizeInUse / pageSize;
        regionSlots.push_back({ previousEnd, previousEnd + numSlots, dataRegion.buffer });
    }
    // __LINKEDIT
    if ( subCache._readOnlyRegion.has_value() ) {
        uint64_t previousEnd = regionSlots.back().end;
        uint64_t numSlots = subCache._readOnlyRegion->sizeInUse / pageSize;
        regionSlots.push_back({ previousEnd, previousEnd + numSlots, subCache._readOnlyRegion->buffer });
    }
    // local symbols
    if ( (_localSymbolsRegion.sizeInUse != 0) && (&subCache == &_localSymbolsSubCache) ) {
        uint64_t previousEnd = regionSlots.back().end;
        uint64_t numSlots = _localSymbolsRegion.sizeInUse / pageSize;
        regionSlots.push_back({ previousEnd, previousEnd + numSlots, _localSymbolsRegion.buffer });
    }

    auto codeSignPage = ^(size_t i) {
        // move to correct region
        for (const SlotRange& slotRange : regionSlots) {
            if ( (i >= slotRange.start) && (i < slotRange.end) ) {
                const uint8_t* code = slotRange.buffer + ((i - slotRange.start) * pageSize);

                CCDigest(dscDigestFormat, code, pageSize, hashSlot + (i * dscHashSize));

                if ( agile ) {
                    CCDigest(kCCDigestSHA256, code, pageSize, hash256Slot + (i * CS_HASH_SIZE_SHA256));
                }
                return;
            }
        }
        assert(0 && "Out of range slot");
    };

    // compute hashes
    dispatch_apply(slotCount, DISPATCH_APPLY_AUTO, ^(size_t i) {
        codeSignPage(i);
    });

    // Now that we have a code signature, compute a cache UUID by hashing the code signature blob
    {
        uint8_t* uuidLoc = cache->uuid;
        assert(uuid_is_null(uuidLoc));
        static_assert(offsetof(dyld_cache_header, uuid) / CS_PAGE_SIZE_4K == 0, "uuid is expected in the first page of the cache");
        uint8_t fullDigest[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256((const void*)cd, (unsigned)cdSize, fullDigest);
        memcpy(uuidLoc, fullDigest, 16);
        // <rdar://problem/6723729> uuids should conform to RFC 4122 UUID version 4 & UUID version 5 formats
        uuidLoc[6] = ( uuidLoc[6] & 0x0F ) | ( 3 << 4 );
        uuidLoc[8] = ( uuidLoc[8] & 0x3F ) | 0x80;

        // Now codesign page 0 again, because we modified it by setting uuid in header
        codeSignPage(0);
    }

    // hash of entire code directory (cdHash) uses same hash as each page
    uint8_t fullCdHash[dscHashSize];
    CCDigest(dscDigestFormat, (const uint8_t*)cd, cdSize, fullCdHash);
    // Note: cdHash is defined as first 20 bytes of hash
    memcpy(subCache._cdHashFirst, fullCdHash, 20);
    if ( agile ) {
        uint8_t fullCdHash256[CS_HASH_SIZE_SHA256];
        CCDigest(kCCDigestSHA256, (const uint8_t*)cd256, cd256Size, fullCdHash256);
        // Note: cdHash is defined as first 20 bytes of hash, even for sha256
        memcpy(subCache._cdHashSecond, fullCdHash256, 20);
    }
    else {
        memset(subCache._cdHashSecond, 0, 20);
    }
}

const bool SharedCacheBuilder::agileSignature() const
{
    return _options.codeSigningDigestMode == DyldSharedCache::Agile;
}

static const std::string cdHash(const uint8_t hash[20])
{
    char buff[48];
    for (int i = 0; i < 20; ++i)
        sprintf(&buff[2*i], "%2.2x", hash[i]);
    return buff;
}

const std::string SharedCacheBuilder::SubCache::cdHashFirst() const
{
    return cdHash(_cdHashFirst);
}

const std::string SharedCacheBuilder::SubCache::cdHashSecond() const
{
    return cdHash(_cdHashSecond);
}

const std::string SharedCacheBuilder::SubCache::uuid() const
{
    dyld_cache_header* cache = (dyld_cache_header*)_readExecuteRegion.buffer;
    uuid_string_t uuidStr;
    uuid_unparse(cache->uuid, uuidStr);
    return uuidStr;
}

void SharedCacheBuilder::forEachDylibInfo(void (^callback)(const CacheBuilder::DylibInfo& dylib, Diagnostics& dylibDiag,
                                                           ASLR_Tracker& dylibASLRTracker)) {
    for (const DylibInfo& dylibInfo : _sortedDylibs) {
        // The shared cache builder doesn't use per-dylib errors right now
        // so just share the global diagnostics
        callback(dylibInfo, _diagnostics, *dylibInfo._aslrTracker);
    }
}



template <typename P>
bool SharedCacheBuilder::makeRebaseChainV2(uint8_t* pageContent, uint16_t lastLocationOffset, uint16_t offset, const dyld_cache_slide_info2* info,
                                           const CacheBuilder::ASLR_Tracker& aslrTracker)
{
    typedef typename P::uint_t     pint_t;

    const pint_t   deltaMask    = (pint_t)(info->delta_mask);
    const pint_t   valueMask    = ~deltaMask;
    const pint_t   valueAdd     = (pint_t)(info->value_add);
    const unsigned deltaShift   = __builtin_ctzll(deltaMask) - 2;
    const uint32_t maxDelta     = (uint32_t)(deltaMask >> deltaShift);

    pint_t* lastLoc = (pint_t*)&pageContent[lastLocationOffset+0];
    pint_t lastValue = (pint_t)P::getP(*lastLoc);
    if ( (lastValue - valueAdd) & deltaMask ) {
        std::string dylibName;
        std::string segName;
        findDylibAndSegment((void*)pageContent, dylibName, segName);
        _diagnostics.error("rebase pointer (0x%0lX) does not point within cache. lastOffset=0x%04X, seg=%s, dylib=%s\n",
                            (long)lastValue, lastLocationOffset, segName.c_str(), dylibName.c_str());
        return false;
    }
    if ( offset <= (lastLocationOffset+maxDelta) ) {
        // previous location in range, make link from it
        // encode this location into last value
        pint_t delta = offset - lastLocationOffset;
        pint_t newLastValue = ((lastValue - valueAdd) & valueMask) | (delta << deltaShift);
        //warning("  add chain: delta = %d, lastOffset=0x%03X, offset=0x%03X, org value=0x%08lX, new value=0x%08lX",
        //                    offset - lastLocationOffset, lastLocationOffset, offset, (long)lastValue, (long)newLastValue);
        uint8_t highByte;
        if ( aslrTracker.hasHigh8(lastLoc, &highByte) ) {
            uint64_t tbi = (uint64_t)highByte << 56;
            newLastValue |= tbi;
        }
        P::setP(*lastLoc, newLastValue);
        return true;
    }
    //fprintf(stderr, "  too big delta = %d, lastOffset=0x%03X, offset=0x%03X\n", offset - lastLocationOffset, lastLocationOffset, offset);

    // distance between rebase locations is too far
    // see if we can make a chain from non-rebase locations
    uint16_t nonRebaseLocationOffsets[1024];
    unsigned nrIndex = 0;
    for (uint16_t i = lastLocationOffset; i < offset-maxDelta; ) {
        nonRebaseLocationOffsets[nrIndex] = 0;
        for (int j=maxDelta; j > 0; j -= 4) {
            pint_t value = (pint_t)P::getP(*(pint_t*)&pageContent[i+j]);
            if ( value == 0 ) {
                // Steal values of 0 to be used in the rebase chain
                nonRebaseLocationOffsets[nrIndex] = i+j;
                break;
            }
        }
        if ( nonRebaseLocationOffsets[nrIndex] == 0 ) {
            lastValue = (pint_t)P::getP(*lastLoc);
            pint_t newValue = ((lastValue - valueAdd) & valueMask);
            //warning("   no way to make non-rebase delta chain, terminate off=0x%03X, old value=0x%08lX, new value=0x%08lX", lastLocationOffset, (long)value, (long)newValue);
            P::setP(*lastLoc, newValue);
            return false;
        }
        i = nonRebaseLocationOffsets[nrIndex];
        ++nrIndex;
    }

    // we can make chain. go back and add each non-rebase location to chain
    uint16_t prevOffset = lastLocationOffset;
    pint_t* prevLoc = (pint_t*)&pageContent[prevOffset];
    for (unsigned n=0; n < nrIndex; ++n) {
        uint16_t nOffset = nonRebaseLocationOffsets[n];
        assert(nOffset != 0);
        pint_t* nLoc = (pint_t*)&pageContent[nOffset];
        pint_t delta2 = nOffset - prevOffset;
        pint_t value = (pint_t)P::getP(*prevLoc);
        pint_t newValue;
        if ( value == 0 )
            newValue = (delta2 << deltaShift);
        else
            newValue = ((value - valueAdd) & valueMask) | (delta2 << deltaShift);
        //warning("    non-rebase delta = %d, to off=0x%03X, old value=0x%08lX, new value=0x%08lX", delta2, nOffset, (long)value, (long)newValue);
        P::setP(*prevLoc, newValue);
        prevOffset = nOffset;
        prevLoc = nLoc;
    }
    pint_t delta3 = offset - prevOffset;
    pint_t value = (pint_t)P::getP(*prevLoc);
    pint_t newValue;
    if ( value == 0 )
        newValue = (delta3 << deltaShift);
    else
        newValue = ((value - valueAdd) & valueMask) | (delta3 << deltaShift);
    //warning("    non-rebase delta = %d, to off=0x%03X, old value=0x%08lX, new value=0x%08lX", delta3, offset, (long)value, (long)newValue);
    P::setP(*prevLoc, newValue);

    return true;
}


template <typename P>
void SharedCacheBuilder::addPageStartsV2(uint8_t* pageContent, const bool bitmap[], const dyld_cache_slide_info2* info,
                                         const CacheBuilder::ASLR_Tracker& aslrTracker,
                                         std::vector<uint16_t>& pageStarts, std::vector<uint16_t>& pageExtras)
{
    typedef typename P::uint_t     pint_t;

    const pint_t   deltaMask    = (pint_t)(info->delta_mask);
    const pint_t   valueMask    = ~deltaMask;
    const uint32_t pageSize     = info->page_size;
    const pint_t   valueAdd     = (pint_t)(info->value_add);

    uint16_t startValue = DYLD_CACHE_SLIDE_PAGE_ATTR_NO_REBASE;
    uint16_t lastLocationOffset = 0xFFFF;
    for(uint32_t i=0; i < pageSize/4; ++i) {
        unsigned offset = i*4;
        if ( bitmap[i] ) {
            if ( startValue == DYLD_CACHE_SLIDE_PAGE_ATTR_NO_REBASE ) {
                // found first rebase location in page
                startValue = i;
            }
            else if ( !makeRebaseChainV2<P>(pageContent, lastLocationOffset, offset, info, aslrTracker) ) {
                // can't record all rebasings in one chain
                if ( (startValue & DYLD_CACHE_SLIDE_PAGE_ATTR_EXTRA) == 0 ) {
                    // switch page_start to "extras" which is a list of chain starts
                    unsigned indexInExtras = (unsigned)pageExtras.size();
                    if ( indexInExtras > 0x3FFF ) {
                        _diagnostics.error("rebase overflow in v2 page extras");
                        return;
                    }
                    pageExtras.push_back(startValue);
                    startValue = indexInExtras | DYLD_CACHE_SLIDE_PAGE_ATTR_EXTRA;
                }
                pageExtras.push_back(i);
            }
            lastLocationOffset = offset;
        }
    }
    if ( lastLocationOffset != 0xFFFF ) {
        // mark end of chain
        pint_t* lastLoc = (pint_t*)&pageContent[lastLocationOffset];
        pint_t lastValue = (pint_t)P::getP(*lastLoc);
        pint_t newValue = ((lastValue - valueAdd) & valueMask);
        P::setP(*lastLoc, newValue);
    }
    if ( startValue & DYLD_CACHE_SLIDE_PAGE_ATTR_EXTRA ) {
        // add end bit to extras
        pageExtras.back() |= DYLD_CACHE_SLIDE_PAGE_ATTR_END;
    }
    pageStarts.push_back(startValue);
}

template <typename P>
void SharedCacheBuilder::writeSlideInfoV2(SubCache& subCache)
{
    const CacheBuilder::ASLR_Tracker& aslrTracker = subCache._aslrTracker;
    const bool* bitmapForAllDataRegions = aslrTracker.bitmap();
    unsigned dataPageCountForAllDataRegions = aslrTracker.dataPageCount();

    const uint32_t  pageSize = aslrTracker.pageSize();
    const uint8_t*  firstDataRegionBuffer = subCache.firstDataRegion()->buffer;
    for (uint32_t dataRegionIndex = 0; dataRegionIndex != subCache._dataRegions.size(); ++dataRegionIndex) {
        Region& dataRegion = subCache._dataRegions[dataRegionIndex];

        // fill in fixed info
        assert(dataRegion.slideInfoFileOffset != 0);
        assert((dataRegion.sizeInUse % pageSize) == 0);
        unsigned dataPageCount = (uint32_t)dataRegion.sizeInUse / pageSize;
        dyld_cache_slide_info2* info = (dyld_cache_slide_info2*)dataRegion.slideInfoBuffer;
        info->version    = 2;
        info->page_size  = pageSize;
        info->delta_mask = _archLayout->pointerDeltaMask;
        info->value_add  = _archLayout->useValueAdd ? _archLayout->sharedMemoryStart : 0;

        // set page starts and extras for each page
        std::vector<uint16_t> pageStarts;
        std::vector<uint16_t> pageExtras;
        pageStarts.reserve(dataPageCount);

        const size_t bitmapEntriesPerPage = (sizeof(bool)*(pageSize/4));
        uint8_t* pageContent = dataRegion.buffer;
        unsigned numPagesFromFirstDataRegion = (uint32_t)(dataRegion.buffer - firstDataRegionBuffer) / pageSize;
        assert((numPagesFromFirstDataRegion + dataPageCount) <= dataPageCountForAllDataRegions);
        const bool* bitmapForRegion = (const bool*)bitmapForAllDataRegions + (bitmapEntriesPerPage * numPagesFromFirstDataRegion);
        const bool* bitmapForPage = bitmapForRegion;
        for (unsigned i=0; i < dataPageCount; ++i) {
            //warning("page[%d]", i);
            addPageStartsV2<P>(pageContent, bitmapForPage, info, aslrTracker, pageStarts, pageExtras);
            if ( _diagnostics.hasError() ) {
                return;
            }
            pageContent += pageSize;
            bitmapForPage += (sizeof(bool)*(pageSize/4));
        }

        // fill in computed info
        info->page_starts_offset = sizeof(dyld_cache_slide_info2);
        info->page_starts_count  = (unsigned)pageStarts.size();
        info->page_extras_offset = (unsigned)(sizeof(dyld_cache_slide_info2)+pageStarts.size()*sizeof(uint16_t));
        info->page_extras_count  = (unsigned)pageExtras.size();
        uint16_t* pageStartsBuffer = (uint16_t*)((char*)info + info->page_starts_offset);
        uint16_t* pageExtrasBuffer = (uint16_t*)((char*)info + info->page_extras_offset);
        for (unsigned i=0; i < pageStarts.size(); ++i)
            pageStartsBuffer[i] = pageStarts[i];
        for (unsigned i=0; i < pageExtras.size(); ++i)
            pageExtrasBuffer[i] = pageExtras[i];
        // update header with final size
        uint64_t slideInfoSize = align(info->page_extras_offset + pageExtras.size()*sizeof(uint16_t), _archLayout->sharedRegionAlignP2);
        dataRegion.slideInfoFileSize = slideInfoSize;
        if ( dataRegion.slideInfoFileSize > dataRegion.slideInfoBufferSizeAllocated ) {
            _diagnostics.error("kernel slide info overflow buffer");
        }
        // Update the mapping entry on the cache header
        const dyld_cache_header*       cacheHeader = (dyld_cache_header*)subCache._readExecuteRegion.buffer;
        dyld_cache_mapping_and_slide_info* slidableMappings = (dyld_cache_mapping_and_slide_info*)(subCache._readExecuteRegion.buffer + cacheHeader->mappingWithSlideOffset);
        slidableMappings[1 + dataRegionIndex].slideInfoFileSize = dataRegion.slideInfoFileSize;
        //fprintf(stderr, "pageCount=%u, page_starts_count=%lu, page_extras_count=%lu\n", dataPageCount, pageStarts.size(), pageExtras.size());
    }
}

#if SUPPORT_ARCH_arm64_32 || SUPPORT_ARCH_armv7k
// fits in to int16_t
static bool smallValue(uint64_t value)
{
    uint32_t high = (value & 0xFFFF8000);
    return (high == 0) || (high == 0xFFFF8000);
}

template <typename P>
bool SharedCacheBuilder::makeRebaseChainV4(uint8_t* pageContent, uint16_t lastLocationOffset, uint16_t offset, const dyld_cache_slide_info4* info)
{
    typedef typename P::uint_t     pint_t;

    const pint_t   deltaMask    = (pint_t)(info->delta_mask);
    const pint_t   valueMask    = ~deltaMask;
    const pint_t   valueAdd     = (pint_t)(info->value_add);
    const unsigned deltaShift   = __builtin_ctzll(deltaMask) - 2;
    const uint32_t maxDelta     = (uint32_t)(deltaMask >> deltaShift);

    pint_t* lastLoc = (pint_t*)&pageContent[lastLocationOffset+0];
    pint_t lastValue = (pint_t)P::getP(*lastLoc);
    if ( (lastValue - valueAdd) & deltaMask ) {
        std::string dylibName;
        std::string segName;
        findDylibAndSegment((void*)pageContent, dylibName, segName);
        _diagnostics.error("rebase pointer does not point within cache. lastOffset=0x%04X, seg=%s, dylib=%s\n",
                            lastLocationOffset, segName.c_str(), dylibName.c_str());
        return false;
    }
    if ( offset <= (lastLocationOffset+maxDelta) ) {
        // previous location in range, make link from it
        // encode this location into last value
        pint_t delta = offset - lastLocationOffset;
        pint_t newLastValue = ((lastValue - valueAdd) & valueMask) | (delta << deltaShift);
        //warning("  add chain: delta = %d, lastOffset=0x%03X, offset=0x%03X, org value=0x%08lX, new value=0x%08lX",
        //                    offset - lastLocationOffset, lastLocationOffset, offset, (long)lastValue, (long)newLastValue);
        P::setP(*lastLoc, newLastValue);
        return true;
    }
    //fprintf(stderr, "  too big delta = %d, lastOffset=0x%03X, offset=0x%03X\n", offset - lastLocationOffset, lastLocationOffset, offset);

    // distance between rebase locations is too far
    // see if we can make a chain from non-rebase locations
    uint16_t nonRebaseLocationOffsets[1024];
    unsigned nrIndex = 0;
    for (uint16_t i = lastLocationOffset; i < offset-maxDelta; ) {
        nonRebaseLocationOffsets[nrIndex] = 0;
        for (int j=maxDelta; j > 0; j -= 4) {
            pint_t value = (pint_t)P::getP(*(pint_t*)&pageContent[i+j]);
            if ( smallValue(value) ) {
                // Steal values of 0 to be used in the rebase chain
                nonRebaseLocationOffsets[nrIndex] = i+j;
                break;
            }
        }
        if ( nonRebaseLocationOffsets[nrIndex] == 0 ) {
            lastValue = (pint_t)P::getP(*lastLoc);
            pint_t newValue = ((lastValue - valueAdd) & valueMask);
            //fprintf(stderr, "   no way to make non-rebase delta chain, terminate off=0x%03X, old value=0x%08lX, new value=0x%08lX\n",
            //                lastLocationOffset, (long)lastValue, (long)newValue);
            P::setP(*lastLoc, newValue);
            return false;
        }
        i = nonRebaseLocationOffsets[nrIndex];
        ++nrIndex;
    }

    // we can make chain. go back and add each non-rebase location to chain
    uint16_t prevOffset = lastLocationOffset;
    pint_t* prevLoc = (pint_t*)&pageContent[prevOffset];
    for (unsigned n=0; n < nrIndex; ++n) {
        uint16_t nOffset = nonRebaseLocationOffsets[n];
        assert(nOffset != 0);
        pint_t* nLoc = (pint_t*)&pageContent[nOffset];
        uint32_t delta2 = nOffset - prevOffset;
        pint_t value = (pint_t)P::getP(*prevLoc);
        pint_t newValue;
        if ( smallValue(value) )
            newValue = (value & valueMask) | (delta2 << deltaShift);
        else
            newValue = ((value - valueAdd) & valueMask) | (delta2 << deltaShift);
        //warning("    non-rebase delta = %d, to off=0x%03X, old value=0x%08lX, new value=0x%08lX", delta2, nOffset, (long)value, (long)newValue);
        P::setP(*prevLoc, newValue);
        prevOffset = nOffset;
        prevLoc = nLoc;
    }
    uint32_t delta3 = offset - prevOffset;
    pint_t value = (pint_t)P::getP(*prevLoc);
    pint_t newValue;
    if ( smallValue(value) )
        newValue = (value & valueMask) | (delta3 << deltaShift);
    else
        newValue = ((value - valueAdd) & valueMask) | (delta3 << deltaShift);
    //warning("    non-rebase delta = %d, to off=0x%03X, old value=0x%08lX, new value=0x%08lX", delta3, offset, (long)value, (long)newValue);
    P::setP(*prevLoc, newValue);

    return true;
}


template <typename P>
void SharedCacheBuilder::addPageStartsV4(uint8_t* pageContent, const bool bitmap[], const dyld_cache_slide_info4* info,
                                         std::vector<uint16_t>& pageStarts, std::vector<uint16_t>& pageExtras)
{
    typedef typename P::uint_t     pint_t;

    const pint_t   deltaMask    = (pint_t)(info->delta_mask);
    const pint_t   valueMask    = ~deltaMask;
    const uint32_t pageSize     = info->page_size;
    const pint_t   valueAdd     = (pint_t)(info->value_add);

    uint16_t startValue = DYLD_CACHE_SLIDE4_PAGE_NO_REBASE;
    uint16_t lastLocationOffset = 0xFFFF;
    for(uint32_t i=0; i < pageSize/4; ++i) {
        unsigned offset = i*4;
        if ( bitmap[i] ) {
            if ( startValue == DYLD_CACHE_SLIDE4_PAGE_NO_REBASE ) {
                // found first rebase location in page
                startValue = i;
            }
            else if ( !makeRebaseChainV4<P>(pageContent, lastLocationOffset, offset, info) ) {
                // can't record all rebasings in one chain
                if ( (startValue & DYLD_CACHE_SLIDE4_PAGE_USE_EXTRA) == 0 ) {
                    // switch page_start to "extras" which is a list of chain starts
                    unsigned indexInExtras = (unsigned)pageExtras.size();
                    if ( indexInExtras >= DYLD_CACHE_SLIDE4_PAGE_INDEX ) {
                        _diagnostics.error("rebase overflow in v4 page extras");
                        return;
                    }
                    pageExtras.push_back(startValue);
                    startValue = indexInExtras | DYLD_CACHE_SLIDE4_PAGE_USE_EXTRA;
                }
                pageExtras.push_back(i);
            }
            lastLocationOffset = offset;
        }
    }
    if ( lastLocationOffset != 0xFFFF ) {
        // mark end of chain
        pint_t* lastLoc = (pint_t*)&pageContent[lastLocationOffset];
        pint_t lastValue = (pint_t)P::getP(*lastLoc);
        pint_t newValue = ((lastValue - valueAdd) & valueMask);
        P::setP(*lastLoc, newValue);
        if ( startValue & DYLD_CACHE_SLIDE4_PAGE_USE_EXTRA ) {
            // add end bit to extras
            pageExtras.back() |= DYLD_CACHE_SLIDE4_PAGE_EXTRA_END;
        }
    }
    pageStarts.push_back(startValue);
}



template <typename P>
void SharedCacheBuilder::writeSlideInfoV4(SubCache& subCache)
{
    const CacheBuilder::ASLR_Tracker& aslrTracker = subCache._aslrTracker;
    const bool* bitmapForAllDataRegions = aslrTracker.bitmap();
    unsigned dataPageCountForAllDataRegions = aslrTracker.dataPageCount();

    const uint32_t  pageSize = aslrTracker.pageSize();
    const uint8_t*  firstDataRegionBuffer = subCache.firstDataRegion()->buffer;
    for (uint32_t dataRegionIndex = 0; dataRegionIndex != subCache._dataRegions.size(); ++dataRegionIndex) {
        Region& dataRegion = subCache._dataRegions[dataRegionIndex];

        // fill in fixed info
        assert(dataRegion.slideInfoFileOffset != 0);
        assert((dataRegion.sizeInUse % pageSize) == 0);
        unsigned dataPageCount = (uint32_t)dataRegion.sizeInUse / pageSize;
        dyld_cache_slide_info4* info = (dyld_cache_slide_info4*)dataRegion.slideInfoBuffer;
        info->version    = 4;
        info->page_size  = pageSize;
        info->delta_mask = _archLayout->pointerDeltaMask;
        info->value_add  = info->value_add  = _archLayout->useValueAdd ? _archLayout->sharedMemoryStart : 0;

        // set page starts and extras for each page
        std::vector<uint16_t> pageStarts;
        std::vector<uint16_t> pageExtras;
        pageStarts.reserve(dataPageCount);
        const size_t bitmapEntriesPerPage = (sizeof(bool)*(pageSize/4));
        uint8_t* pageContent = dataRegion.buffer;
        unsigned numPagesFromFirstDataRegion = (uint32_t)(dataRegion.buffer - firstDataRegionBuffer) / pageSize;
        assert((numPagesFromFirstDataRegion + dataPageCount) <= dataPageCountForAllDataRegions);
        const bool* bitmapForRegion = (const bool*)bitmapForAllDataRegions + (bitmapEntriesPerPage * numPagesFromFirstDataRegion);
        const bool* bitmapForPage = bitmapForRegion;
        for (unsigned i=0; i < dataPageCount; ++i) {
            addPageStartsV4<P>(pageContent, bitmapForPage, info, pageStarts, pageExtras);
            if ( _diagnostics.hasError() ) {
                return;
            }
            pageContent += pageSize;
            bitmapForPage += (sizeof(bool)*(pageSize/4));
        }
        // fill in computed info
        info->page_starts_offset = sizeof(dyld_cache_slide_info4);
        info->page_starts_count  = (unsigned)pageStarts.size();
        info->page_extras_offset = (unsigned)(sizeof(dyld_cache_slide_info4)+pageStarts.size()*sizeof(uint16_t));
        info->page_extras_count  = (unsigned)pageExtras.size();
        uint16_t* pageStartsBuffer = (uint16_t*)((char*)info + info->page_starts_offset);
        uint16_t* pageExtrasBuffer = (uint16_t*)((char*)info + info->page_extras_offset);
        for (unsigned i=0; i < pageStarts.size(); ++i)
            pageStartsBuffer[i] = pageStarts[i];
        for (unsigned i=0; i < pageExtras.size(); ++i)
            pageExtrasBuffer[i] = pageExtras[i];
        // update header with final size
        uint64_t slideInfoSize = align(info->page_extras_offset + pageExtras.size()*sizeof(uint16_t), _archLayout->sharedRegionAlignP2);
        dataRegion.slideInfoFileSize = slideInfoSize;
        if ( dataRegion.slideInfoFileSize > dataRegion.slideInfoBufferSizeAllocated ) {
            _diagnostics.error("kernel slide info overflow buffer");
        }
        // Update the mapping entry on the cache header
        const dyld_cache_header*       cacheHeader = (dyld_cache_header*)subCache._readExecuteRegion.buffer;
        dyld_cache_mapping_and_slide_info* slidableMappings = (dyld_cache_mapping_and_slide_info*)(subCache._readExecuteRegion.buffer + cacheHeader->mappingWithSlideOffset);
        slidableMappings[1 + dataRegionIndex].slideInfoFileSize = dataRegion.slideInfoFileSize;
        //fprintf(stderr, "pageCount=%u, page_starts_count=%lu, page_extras_count=%lu\n", dataPageCount, pageStarts.size(), pageExtras.size());
    }
}
#endif

/*
void CacheBuilder::writeSlideInfoV1()
{
    // build one 128-byte bitmap per page (4096) of DATA
    uint8_t* const dataStart = (uint8_t*)_buffer.get() + regions[1].fileOffset;
    uint8_t* const dataEnd   = dataStart + regions[1].size;
    const long bitmapSize = (dataEnd - dataStart)/(4*8);
    uint8_t* bitmap = (uint8_t*)calloc(bitmapSize, 1);
    for (void* p : _pointersForASLR) {
        if ( (p < dataStart) || ( p > dataEnd) )
            terminate("DATA pointer for sliding, out of range\n");
        long offset = (long)((uint8_t*)p - dataStart);
        if ( (offset % 4) != 0 )
            terminate("pointer not 4-byte aligned in DATA offset 0x%08lX\n", offset);
        long byteIndex = offset / (4*8);
        long bitInByte =  (offset % 32) >> 2;
        bitmap[byteIndex] |= (1 << bitInByte);
    }

    // allocate worst case size block of all slide info
    const unsigned entry_size = 4096/(8*4); // 8 bits per byte, possible pointer every 4 bytes.
    const unsigned toc_count = (unsigned)bitmapSize/entry_size;
    dyld_cache_slide_info* slideInfo = (dyld_cache_slide_info*)((uint8_t*)_buffer + _slideInfoFileOffset);
    slideInfo->version          = 1;
    slideInfo->toc_offset       = sizeof(dyld_cache_slide_info);
    slideInfo->toc_count        = toc_count;
    slideInfo->entries_offset   = (slideInfo->toc_offset+2*toc_count+127)&(-128);
    slideInfo->entries_count    = 0;
    slideInfo->entries_size     = entry_size;
    // append each unique entry
    const dyldCacheSlideInfoEntry* bitmapAsEntries = (dyldCacheSlideInfoEntry*)bitmap;
    dyldCacheSlideInfoEntry* const entriesInSlidInfo = (dyldCacheSlideInfoEntry*)((char*)slideInfo+slideInfo->entries_offset());
    int entry_count = 0;
    for (int i=0; i < toc_count; ++i) {
        const dyldCacheSlideInfoEntry* thisEntry = &bitmapAsEntries[i];
        // see if it is same as one already added
        bool found = false;
        for (int j=0; j < entry_count; ++j) {
            if ( memcmp(thisEntry, &entriesInSlidInfo[j], entry_size) == 0 ) {
                slideInfo->set_toc(i, j);
                found = true;
                break;
            }
        }
        if ( !found ) {
            // append to end
            memcpy(&entriesInSlidInfo[entry_count], thisEntry, entry_size);
            slideInfo->set_toc(i, entry_count++);
        }
    }
    slideInfo->entries_count  = entry_count;
    ::free((void*)bitmap);

    _buffer.header->slideInfoSize = align(slideInfo->entries_offset + entry_count*entry_size, _archLayout->sharedRegionAlignP2);
}

*/


void SharedCacheBuilder::setPointerContentV3(dyld3::MachOLoaded::ChainedFixupPointerOnDisk* loc, uint64_t targetVMAddr, size_t next,
                                             SubCache& subCache)
{
    const CacheBuilder::ASLR_Tracker& aslrTracker = subCache._aslrTracker;
    uint64_t cacheUnslidLoadAddress = _subCaches.front()._readExecuteRegion.unslidLoadAddress;
    assert(targetVMAddr > cacheUnslidLoadAddress);
    assert(targetVMAddr < _subCaches.back().highestVMAddress());

    dyld3::MachOLoaded::ChainedFixupPointerOnDisk tmp;
    uint16_t diversity;
    bool     hasAddrDiv;
    uint8_t  key;
    if ( aslrTracker.hasAuthData(loc, &diversity, &hasAddrDiv, &key) ) {
        // if base cache address cannot fit into target, then use offset
        tmp.arm64e.authRebase.target = cacheUnslidLoadAddress;
        if (  tmp.arm64e.authRebase.target != cacheUnslidLoadAddress )
            targetVMAddr -= cacheUnslidLoadAddress;
        loc->arm64e.authRebase.target    = targetVMAddr;
        loc->arm64e.authRebase.diversity = diversity;
        loc->arm64e.authRebase.addrDiv   = hasAddrDiv;
        loc->arm64e.authRebase.key       = key;
        loc->arm64e.authRebase.next      = next;
        loc->arm64e.authRebase.bind      = 0;
        loc->arm64e.authRebase.auth      = 1;
        assert(loc->arm64e.authRebase.target == targetVMAddr && "target truncated");
        assert(loc->arm64e.authRebase.next == next && "next location truncated");
    }
    else {
        uint8_t highByte = 0;
        aslrTracker.hasHigh8(loc, &highByte);
        // if base cache address cannot fit into target, then use offset
        tmp.arm64e.rebase.target = cacheUnslidLoadAddress;
        if ( tmp.arm64e.rebase.target != cacheUnslidLoadAddress )
            targetVMAddr -= cacheUnslidLoadAddress;
        loc->arm64e.rebase.target   = targetVMAddr;
        loc->arm64e.rebase.high8    = highByte;
        loc->arm64e.rebase.next     = next;
        loc->arm64e.rebase.bind     = 0;
        loc->arm64e.rebase.auth     = 0;
        assert(loc->arm64e.rebase.target == targetVMAddr && "target truncated");
        assert(loc->arm64e.rebase.next == next && "next location truncated");
    }
}

uint16_t SharedCacheBuilder::pageStartV3(uint8_t* pageContent, uint32_t pageSize, const bool bitmap[],
                                         SubCache& subCache)
{
    const int maxPerPage = pageSize / 4;
    uint16_t result = DYLD_CACHE_SLIDE_V3_PAGE_ATTR_NO_REBASE;
    dyld3::MachOLoaded::ChainedFixupPointerOnDisk* lastLoc = nullptr;
    for (int i=0; i < maxPerPage; ++i) {
        if ( bitmap[i] ) {
            if ( result == DYLD_CACHE_SLIDE_V3_PAGE_ATTR_NO_REBASE ) {
                // found first rebase location in page
                result = i * 4;
            }
            dyld3::MachOLoaded::ChainedFixupPointerOnDisk* loc = (dyld3::MachOLoaded::ChainedFixupPointerOnDisk*)(pageContent + i*4);
            if ( lastLoc != nullptr ) {
                // convert vmaddr based pointers to arm64e dyld cache chains
                setPointerContentV3(lastLoc, lastLoc->raw64, loc - lastLoc, subCache);
            }
            lastLoc = loc;
        }
    }
    if ( lastLoc != nullptr ) {
        // convert vmaddr based pointers to arm64e dyld cache chain, and mark end of chain
        setPointerContentV3(lastLoc, lastLoc->raw64, 0, subCache);
    }
    return result;
}


void SharedCacheBuilder::writeSlideInfoV3(SubCache& subCache)
{
    const CacheBuilder::ASLR_Tracker& aslrTracker = subCache._aslrTracker;
    const bool* bitmapForAllDataRegions = aslrTracker.bitmap();
    unsigned dataPageCountForAllDataRegions = aslrTracker.dataPageCount();

    const uint32_t  pageSize = aslrTracker.pageSize();
    const uint8_t*  firstDataRegionBuffer = subCache.firstDataRegion()->buffer;
    for (uint32_t dataRegionIndex = 0; dataRegionIndex != subCache._dataRegions.size(); ++dataRegionIndex) {
        Region& dataRegion = subCache._dataRegions[dataRegionIndex];
        // fprintf(stderr, "writeSlideInfoV3: %s 0x%llx->0x%llx\n", dataRegion.name.c_str(), dataRegion.cacheFileOffset, dataRegion.cacheFileOffset + dataRegion.sizeInUse);
        // fill in fixed info
        assert(dataRegion.slideInfoFileOffset != 0);
        assert((dataRegion.sizeInUse % pageSize) == 0);
        unsigned dataPageCount = (uint32_t)dataRegion.sizeInUse / pageSize;
        dyld_cache_slide_info3* info = (dyld_cache_slide_info3*)dataRegion.slideInfoBuffer;
        info->version           = 3;
        info->page_size         = pageSize;
        info->page_starts_count = dataPageCount;
        info->auth_value_add    = _archLayout->sharedMemoryStart;

        // fill in per-page starts
        const size_t bitmapEntriesPerPage = (sizeof(bool)*(pageSize/4));
        uint8_t* pageContent = dataRegion.buffer;
        unsigned numPagesFromFirstDataRegion = (uint32_t)(dataRegion.buffer - firstDataRegionBuffer) / pageSize;
        assert((numPagesFromFirstDataRegion + dataPageCount) <= dataPageCountForAllDataRegions);
        const bool* bitmapForRegion = (const bool*)bitmapForAllDataRegions + (bitmapEntriesPerPage * numPagesFromFirstDataRegion);
        const bool* bitmapForPage = bitmapForRegion;
        //for (unsigned i=0; i < dataPageCount; ++i) {
        dispatch_apply(dataPageCount, DISPATCH_APPLY_AUTO, ^(size_t i) {
            info->page_starts[i] = pageStartV3(pageContent + (i * pageSize), pageSize, bitmapForPage + (i * bitmapEntriesPerPage), subCache);
        });

        // update region with final size
        dataRegion.slideInfoFileSize = align(__offsetof(dyld_cache_slide_info3, page_starts[dataPageCount]), _archLayout->sharedRegionAlignP2);
        if ( dataRegion.slideInfoFileSize > dataRegion.slideInfoBufferSizeAllocated ) {
            _diagnostics.error("kernel slide info overflow buffer");
        }
        // Update the mapping entry on the cache header
        const dyld_cache_header*       cacheHeader = (dyld_cache_header*)subCache._readExecuteRegion.buffer;
        dyld_cache_mapping_and_slide_info* slidableMappings = (dyld_cache_mapping_and_slide_info*)(subCache._readExecuteRegion.buffer + cacheHeader->mappingWithSlideOffset);
        slidableMappings[1 + dataRegionIndex].slideInfoFileSize = dataRegion.slideInfoFileSize;
    }
}
