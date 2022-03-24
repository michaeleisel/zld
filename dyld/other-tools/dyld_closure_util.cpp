/*
 * Copyright (c) 2017 Apple Inc. All rights reserved.
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


#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syslimits.h>
#include <mach-o/arch.h>
#include <mach-o/loader.h>
#include <mach-o/dyld_priv.h>
#include <bootstrap.h>
#include <mach/mach.h>
#include <dispatch/dispatch.h>

#include <map>
#include <vector>

#include "DyldSharedCache.h"
#include "FileUtils.h"
#include "StringUtils.h"
#include "ClosureFileSystemPhysical.h"
#include "PrebuiltLoader.h"
#include "DyldProcessConfig.h"
#include "DyldRuntimeState.h"
#include "JSONWriter.h"

using dyld3::Array;
using namespace dyld4;


static void usage()
{
    printf("dyld_closure_util program to create or view dyld3 closures\n");
    printf("  mode:\n");
    printf("    -create_closure <prog-path>            # create a closure for the specified main executable\n");
    printf("    -list_dyld_cache_closures              # list all launch closures in the dyld shared cache with size\n");
    printf("    -print_dyld_cache_closure <prog-path>  # find closure for specified program in dyld cache and print as JSON\n");
    printf("    -print_dyld_cache_dylib <dylib-path>   # print specified cached dylib as JSON\n");
    printf("    -print_dyld_cache_dylibs               # print all cached dylibs as JSON\n");
    printf("    -print_closure_file <closure-path>     # print specified program closure as JSON\n");
    printf("  options:\n");
    printf("    -cache_file <cache-path>               # path to cache file to use (default is current cache)\n");
    printf("    -build_root <path-prefix>              # when building a closure, the path prefix when runtime volume is not current boot volume\n");
    printf("    -env <var=value>                       # when building a closure, DYLD_* env vars to assume\n");
    printf("    -verbose_fixups                        # for use with -print* options to force printing fixups\n");
    printf("    -no_at_paths                           # when building a closure, simulate security not allowing @path expansion\n");
    printf("    -no_fallback_paths                     # when building a closure, simulate security not allowing default fallback paths\n");
    printf("    -allow_insertion_failures              # when building a closure, simulate security allowing unloadable DYLD_INSERT_LIBRARIES to be ignored\n");
}

int main(int argc, const char* argv[])
{
    const char*               cacheFilePath = nullptr;
    const char*               inputMainExecutablePath = nullptr;
    const char*               printCacheClosure = nullptr;
    const char*               printCachedDylib = nullptr;
    const char*               fsRootPath = nullptr;
    const char*               fsOverlayPath = nullptr;
    const char*               printClosureFile = nullptr;
    bool                      listCacheClosures = false;
    bool                      printCachedDylibs = false;
    bool                      verboseFixups = false;
    bool                      allowAtPaths = true;
    bool                      allowFallbackPaths = true;
    bool                      allowInsertionFailures = false;
    bool                      printRaw = false;
    std::vector<const char*>  envArgs;
    char                      fsRootRealPath[PATH_MAX];
    char                      fsOverlayRealPath[PATH_MAX];

    if ( argc == 1 ) {
        usage();
        return 0;
    }

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if ( strcmp(arg, "-cache_file") == 0 ) {
            cacheFilePath = argv[++i];
            if ( cacheFilePath == nullptr ) {
                fprintf(stderr, "-cache_file option requires path to cache file\n");
                return 1;
            }
        }
        else if ( strcmp(arg, "-create_closure") == 0 ) {
            inputMainExecutablePath = argv[++i];
            if ( inputMainExecutablePath == nullptr ) {
                fprintf(stderr, "-create_closure option requires a path to an executable\n");
                return 1;
            }
        }
        else if ( strcmp(arg, "-verbose_fixups") == 0 ) {
           verboseFixups = true;
        }
        else if ( strcmp(arg, "-no_at_paths") == 0 ) {
            allowAtPaths = false;
        }
        else if ( strcmp(arg, "-no_fallback_paths") == 0 ) {
            allowFallbackPaths = false;
        }
        else if ( strcmp(arg, "-allow_insertion_failures") == 0 ) {
            allowInsertionFailures = true;
        }
        else if ( strcmp(arg, "-raw") == 0 ) {
            printRaw = true;
        }
        else if ( strcmp(arg, "-fs_root") == 0 ) {
            fsRootPath = argv[++i];
            if ( fsRootPath == nullptr ) {
                fprintf(stderr, "-fs_root option requires a path\n");
                return 1;
            }
            if ( realpath(fsRootPath, fsRootRealPath) == nullptr ) {
                fprintf(stderr, "-fs_root option requires a real path\n");
                return 1;
            }
            fsRootPath = fsRootRealPath;
        }
        else if ( strcmp(arg, "-fs_overlay") == 0 ) {
            fsOverlayPath = argv[++i];
            if ( fsOverlayPath == nullptr ) {
                fprintf(stderr, "-fs_overlay option requires a path\n");
                return 1;
            }
            if ( realpath(fsOverlayPath, fsOverlayRealPath) == nullptr ) {
                fprintf(stderr, "-fs_root option requires a real path\n");
                return 1;
            }
            fsOverlayPath = fsOverlayRealPath;
        }
        else if ( strcmp(arg, "-list_dyld_cache_closures") == 0 ) {
            listCacheClosures = true;
        }
        else if ( strcmp(arg, "-print_dyld_cache_closure") == 0 ) {
            printCacheClosure = argv[++i];
            if ( printCacheClosure == nullptr ) {
                fprintf(stderr, "-print_dyld_cache_closure option requires a path \n");
                return 1;
            }
        }
        else if ( strcmp(arg, "-print_closure_file") == 0 ) {
            printClosureFile = argv[++i];
            if ( printClosureFile == nullptr ) {
                fprintf(stderr, "-print_closure_file option requires a path \n");
                return 1;
            }
        }
        else if ( strcmp(arg, "-print_dyld_cache_dylibs") == 0 ) {
            printCachedDylibs = true;
        }
        else if ( strcmp(arg, "-print_dyld_cache_dylib") == 0 ) {
            printCachedDylib = argv[++i];
            if ( printCachedDylib == nullptr ) {
                fprintf(stderr, "-print_dyld_cache_dylib option requires a path \n");
                return 1;
            }
        }
        else if ( strcmp(arg, "-env") == 0 ) {
            const char* envArg = argv[++i];
            if ( (envArg == nullptr) || (strchr(envArg, '=') == nullptr) ) {
                fprintf(stderr, "-env option requires KEY=VALUE\n");
                return 1;
            }
            envArgs.push_back(envArg);
        }
        else {
            fprintf(stderr, "unknown option %s\n", arg);
            return 1;
        }
    }

    envArgs.push_back(nullptr);

    std::vector<const DyldSharedCache*> dyldCaches;
    const DyldSharedCache* dyldCache = nullptr;
    bool dyldCacheIsLive = true;
    if ( cacheFilePath != nullptr ) {
        dyldCaches = DyldSharedCache::mapCacheFiles(cacheFilePath);
        // mapCacheFile prints an error if something goes wrong, so just return in that case.
        if ( dyldCaches.empty() )
            return 1;
        dyldCache = dyldCaches.front();
        dyldCacheIsLive = false;
    }
    else {
        size_t len;
        dyldCache = (DyldSharedCache*)_dyld_get_shared_cache_range(&len);
    }

    const MachOAnalyzer* mainMA = nullptr;
    if ( dyldCache ) {
        // gracefully handling older dyld caches
        if ( dyldCache->header.mappingOffset < 0x170 ) {
            fprintf(stderr, "dyld_closure_util: can't operate against an old (pre-dyld4) dyld cache\n");
            exit(1);
        }

        // HACK: use libSystem.dylib from cache as main executable to bootstrap state
        uint32_t imageIndex;
        if ( dyldCache->hasImagePath("/usr/lib/libSystem.B.dylib", imageIndex) ) {
            uint64_t ignore1;
            uint64_t ignore2;
            mainMA = (MachOAnalyzer*)dyldCache->getIndexedImageEntry(imageIndex, ignore1, ignore2);
        }
    }

    KernelArgs            kernArgs(mainMA, {"test.exe"}, {}, {});
    SyscallDelegate       osDelegate;
    osDelegate._dyldCache   = dyldCache;
    osDelegate._rootPath    = fsRootPath;
    osDelegate._overlayPath = fsOverlayPath;

    __block ProcessConfig  config(&kernArgs, osDelegate);
    RuntimeState           state(config);

     if ( inputMainExecutablePath != nullptr ) {
        config.reset(mainMA, inputMainExecutablePath, osDelegate._dyldCache);
        state.resetCachedDylibsArrays();

        // Load the executable from disk
        Diagnostics launchDiag;
        Loader::LoadOptions options;
        options.staticLinkage   = true;
        options.launching       = true;
        options.canBeExecutable = true;
        if ( Loader* mainLoader = JustInTimeLoader::makeJustInTimeLoaderDisk(launchDiag, state, inputMainExecutablePath, options) ) {
            state.setMainLoader(mainLoader);

            // platform was a guess from libSystem.dylib, now we have the actual binary loaded, use its platform
            mainLoader->loadAddress(state)->forEachSupportedPlatform(^(dyld3::Platform plat, uint32_t minOS, uint32_t sdk) {
                const dyld3::Platform* p = &config.process.platform;
                *(dyld3::Platform*)p = plat;
            });

            // now that main executable is loaded, use its actual platform as the global platform
            if ( state.config.process.platform == dyld3::Platform::macOS ) {
                mainLoader->loadAddress(state)->forEachSupportedPlatform(^(dyld3::Platform exePlatform, uint32_t minOS, uint32_t sdk) {
                    config.process.platform = exePlatform;
                });
            }
            __block MissingPaths missingPaths;
            auto missingLogger = ^(const char* mustBeMissingPath) {
                missingPaths.addPath(mustBeMissingPath);
            };
            Loader::LoadChain    loadChainMain { nullptr, mainLoader };
            options.canBeDylib      = true;
            options.canBeExecutable = false;
            options.rpathStack      = &loadChainMain;
            options.pathNotFoundHandler = missingLogger;
            mainLoader->loadDependents(launchDiag, state, options);
            if ( launchDiag.hasError() ) {
                fprintf(stderr, "dyld_closure_util: can't build PrebuiltLoader for '%s': %s\n", inputMainExecutablePath, launchDiag.errorMessageCStr());
                exit(1);
            }
            const PrebuiltLoaderSet* prebuiltAppSet = PrebuiltLoaderSet::makeLaunchSet(launchDiag, state, missingPaths);
            if ( launchDiag.hasError() ) {
                fprintf(stderr, "dyld_closure_util: can't build PrebuiltLoaderSet for '%s': %s\n", inputMainExecutablePath, launchDiag.errorMessageCStr());
                exit(1);
            }
            if ( prebuiltAppSet != nullptr ) {
                state.setProcessPrebuiltLoaderSet(prebuiltAppSet);
                // Note dyld_closure_builder parses the JSON, so we can't print comments by default here
                prebuiltAppSet->print(state, stdout, /* printComments */ false);
            }
        }
        else {
            fprintf(stderr, "dyld_closure_util: can't find '%s'\n", inputMainExecutablePath);
            exit(1);
        }
        if ( launchDiag.hasError() ) {
            fprintf(stderr, "dyld_closure_util: can't build PrebuiltLoaderSet for '%s': %s\n", inputMainExecutablePath, launchDiag.errorMessageCStr());
            exit(1);
        }
    }
    else if ( printCacheClosure ) {
        if ( const dyld4::PrebuiltLoaderSet* pbls = config.dyldCache.addr->findLaunchLoaderSet(printCacheClosure) ) {
            state.setProcessPrebuiltLoaderSet(pbls);
            pbls->print(state, stdout, /* printComments */ true);
        }
        else {
            fprintf(stderr, "dyld_closure_util: no PrebuiltLoaderSet in cache for %s\n", printCacheClosure);
        }
    }
    else if ( printClosureFile ) {
        size_t       mappedSize;
        Diagnostics  diag;
        if ( const dyld4::PrebuiltLoaderSet* pbls = (dyld4::PrebuiltLoaderSet*)config.syscall.mapFileReadOnly(diag, printClosureFile, &mappedSize) ) {
            state.setProcessPrebuiltLoaderSet(pbls);
            pbls->print(state, stdout, /* printComments */ true);
            config.syscall.unmapFile(pbls, mappedSize);
        }
        else {
            fprintf(stderr, "dyld_closure_util: no PrebuiltLoaderSet at %s\n", printClosureFile);
        }
    }
    else if ( printCachedDylibs ) {
        state.resetCachedDylibsArrays();
        if ( const dyld4::PrebuiltLoaderSet* pbls = state.cachedDylibsPrebuiltLoaderSet()) {
            for (int i=0; i < pbls->loaderCount(); ++i) {
                const dyld4::PrebuiltLoader* pldr = pbls->atIndex(i);
                pldr->print(state, stdout, /* printComments */ true);
            }
        }
    }
    else if ( printCachedDylib != nullptr ) {
        state.resetCachedDylibsArrays();
        if ( const dyld4::PrebuiltLoader* pldr = config.dyldCache.addr->findPrebuiltLoader(printCachedDylib)  ) {
            pldr->print(state, stdout, /* printComments */ true);
        }
        else {
            fprintf(stderr, "no such image found\n");
        }
    }
    else if ( listCacheClosures ) {
        config.dyldCache.addr->forEachLaunchLoaderSet(^(const char* runtimePath, const PrebuiltLoaderSet* pbls) {
            printf("%6lu  %s\n", pbls->size(), runtimePath);
        });
    }
    return 0;
}
