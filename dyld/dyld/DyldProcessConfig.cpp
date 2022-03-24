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

#include <TargetConditionals.h>
#include <_simple.h>
#include <stdint.h>
#include <dyld/VersionMap.h>
#include <mach-o/dyld_priv.h>
#if BUILDING_DYLD
    #include <sys/socket.h>
    #include <sys/syslog.h>
    #include <sys/uio.h>
    #include <sys/un.h>
    #include <sys/mman.h>
    #include <System/sys/csr.h>
    #include <System/sys/reason.h>
    #include <kern/kcdata.h>
    #include <System/machine/cpu_capabilities.h>
    #if !TARGET_OS_DRIVERKIT
        #include <vproc_priv.h>
    #endif
// no libc header for send() syscall interface
extern "C" ssize_t __sendto(int, const void*, size_t, int, const struct sockaddr*, socklen_t);
#endif

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
    AMFI_DYLD_OUTPUT_ALLOW_LIBRARY_INTERPOSING      = (1 << 6),
};
extern "C" int amfi_check_dyld_policy_self(uint64_t input_flags, uint64_t* output_flags);
    #include "dyldSyscallInterface.h"
#else
    #include <libamfi.h>
#endif

#include "MachOLoaded.h"
#include "MachOAnalyzer.h"
#include "DyldSharedCache.h"
#include "SharedCacheRuntime.h"
#include "Loader.h"
#include "DyldProcessConfig.h"
#include "DebuggerSupport.h"


// based on ANSI-C strstr()
static const char* strrstr(const char* str, const char* sub)
{
    const size_t sublen = strlen(sub);
    for (const char* p = &str[strlen(str)]; p != str; --p) {
        if ( ::strncmp(p, sub, sublen) == 0 )
            return p;
    }
    return nullptr;
}

using dyld3::MachOFile;
using dyld3::Platform;

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

static uint64_t hexToUInt64(const char* startHexByte, const char** endHexByte)
{
    const char* scratch;
    if ( endHexByte == nullptr ) {
        endHexByte = &scratch;
    }
    if ( startHexByte == nullptr )
        return 0;
    uint64_t retval = 0;
    if ( (startHexByte[0] == '0') && (startHexByte[1] == 'x') ) {
        startHexByte += 2;
    }
    *endHexByte = startHexByte + 16;

    //FIXME overrun?
    for ( uint32_t i = 0; i < 16; ++i ) {
        uint8_t value;
        if ( !hexCharToByte(startHexByte[i], value) ) {
            *endHexByte = &startHexByte[i];
            break;
        }
        retval = (retval << 4) + value;
    }
    return retval;
}


namespace dyld4 {


//
// MARK: --- KernelArgs methods ---
//
#if !BUILDING_DYLD
KernelArgs::KernelArgs(const MachOAnalyzer* mh, const std::vector<const char*>& argv, const std::vector<const char*>& envp, const std::vector<const char*>& apple)
    : mainExecutable(mh)
    , argc(argv.size())
{
    assert( argv.size() + envp.size() + apple.size() < MAX_KERNEL_ARGS);

    // build the info passed to dyld on startup the same way the kernel does on the stack
    size_t index = 0;
    for ( const char* arg : argv )
        args[index++] = arg;
    args[index++] = nullptr;

    for ( const char* arg : envp )
        args[index++] = arg;
    args[index++] = nullptr;

    for ( const char* arg : apple )
        args[index++] = arg;
    args[index++] = nullptr;
}
#endif

const char** KernelArgs::findArgv() const
{
    return (const char**)&args[0];
}

const char** KernelArgs::findEnvp() const
{
    // argv array has nullptr at end, so envp starts at argc+1
    return (const char**)&args[argc + 1];
}

const char** KernelArgs::findApple() const
{
    // envp array has nullptr at end, apple starts after that
    const char** p = findEnvp();
    while ( *p != nullptr )
        ++p;
    ++p;
    return p;
}


//
// MARK: --- ProcessConfig methods ---
//
ProcessConfig::ProcessConfig(const KernelArgs* kernArgs, SyscallDelegate& syscallDelegate)
  : syscall(syscallDelegate),
    process(kernArgs, syscallDelegate),
    security(process, syscallDelegate),
    log(process, security, syscallDelegate),
    dyldCache(process, security, log, syscallDelegate),
    pathOverrides(process, security, log, dyldCache, syscallDelegate)
{
}

#if !BUILDING_DYLD
void ProcessConfig::reset(const MachOAnalyzer* mainExe, const char* mainPath, const DyldSharedCache* cache)
{
    process.mainExecutablePath    = mainPath;
    process.mainUnrealPath        = mainPath;
    process.mainExecutable        = mainExe;
    dyldCache.addr                = cache;
    dyldCache.slide               = cache->slide();
}
#endif


//
// MARK: --- Process methods ---
//

static bool defaultDataConst(DyldCommPage commPage)
{
    if ( commPage.forceRWDataConst ) {
        return false;
    } else if ( commPage.forceRWDataConst ) {
        return true;
    } else {
        // __DATA_CONST is enabled by default, as the above boot-args didn't override it
        return true;
    }
}


ProcessConfig::Process::Process(const KernelArgs* kernArgs, SyscallDelegate& syscall)
{
    this->mainExecutable     = kernArgs->mainExecutable;
    this->argc               = (int)kernArgs->argc;
    this->argv               = kernArgs->findArgv();
    this->envp               = kernArgs->findEnvp();
    this->apple              = kernArgs->findApple();
    this->pid                = syscall.getpid();
    this->platform           = this->getMainPlatform();
    this->mainUnrealPath     = this->getMainUnrealPath(syscall);
    this->mainExecutablePath = this->getMainPath(syscall);
    this->dyldPath           = this->getDyldPath(syscall);
    this->progname           = PathOverrides::getLibraryLeafName(this->mainUnrealPath);
    this->catalystRuntime    = this->usesCatalyst();
    this->commPage           = syscall.dyldCommPageFlags();
    this->archs              = this->getMainArchs(syscall);
    this->isTranslated       = syscall.isTranslated();
    this->enableDataConst    = defaultDataConst(this->commPage);
#if TARGET_OS_OSX
    this->proactivelyUseWeakDefMap = (strncmp(progname, "MATLAB",6) == 0); // rdar://81498849
#else
    this->proactivelyUseWeakDefMap = false;
#endif

}

const char* ProcessConfig::Process::appleParam(const char* key) const
{
    return _simple_getenv((const char**)apple, key);
}

const char* ProcessConfig::Process::environ(const char* key) const
{
    return _simple_getenv((const char**)envp, key);
}

void* ProcessConfig::Process::roalloc(size_t size) const
{
#if BUILDING_DYLD
    // warning: fragile code here.  The goal is to have a small buffer that
    // goes onto the end of the __DATA_CONST segment.  That segment is r/w
    // while ProcessConfig is being constructed, then made r/o.
    static uint8_t roBuffer[0x10000] __attribute__((section("__DATA_CONST,__bss")));
    static uint8_t* next = roBuffer;
    assert( next < &roBuffer[0x10000]);
    void* result = next;
    next += size;
    return result;
#else
    return ::malloc(size);
#endif
}

const char* ProcessConfig::Process::strdup(const char* str) const
{
#if BUILDING_DYLD
    size_t  size   = strlen(str)+1;
    char*   result = (char*)roalloc(size);
    ::strcpy(result, str);
    return result;
#else
    return ::strdup(str);
#endif
}

const char* ProcessConfig::Process::pathFromFileHexStrings(SyscallDelegate& sys, const char* encodedFileInfo)
{
    // kernel passes fsID and objID encoded as two hex values (e.g. 0x123,0x456)
    const char* endPtr  = nullptr;
    uint64_t    fsID    = hexToUInt64(encodedFileInfo, &endPtr);
    if ( endPtr != nullptr ) {
        uint64_t objID = hexToUInt64(endPtr+1, &endPtr);
        char pathFromIDs[MAXPATHLEN];
        if ( sys.fsgetpath(pathFromIDs, MAXPATHLEN, fsID, objID) != -1 ) {
            // return read-only copy of absolute path
            return this->strdup(pathFromIDs);
        }
    }

    // something wrong with "executable_file=" or "dyld_file=" encoding
    return nullptr;
}

const char* ProcessConfig::Process::getDyldPath(SyscallDelegate& sys)
{
    // kernel passes fsID and objID of dyld encoded as two hex values (e.g. 0x123,0x456)
    if ( const char* dyldFsIdAndObjId = this->appleParam("dyld_file") ) {
        if ( const char* path = this->pathFromFileHexStrings(sys, dyldFsIdAndObjId) )
            return path;
    }
    // something wrong with "dyld_file=", fallback to default
    return "/usr/lib/dyld";
}

const char* ProcessConfig::Process::getMainPath(SyscallDelegate& sys)
{
    // kernel passes fsID and objID of main executable encoded as two hex values (e.g. 0x123,0x456)
    if ( const char* mainPathFsIdAndObjId = this->appleParam("executable_file") ) {
        if ( const char* path = this->pathFromFileHexStrings(sys, mainPathFsIdAndObjId) )
            return path;
    }
    // something wrong with "executable_file=", fallback to (un)realpath
    char resolvedPath[PATH_MAX];
    if ( sys.realpath(this->mainUnrealPath, resolvedPath) ) {
        return this->strdup(resolvedPath);
    }
    return this->mainUnrealPath;
}

const char* ProcessConfig::Process::getMainUnrealPath(SyscallDelegate& sys)
{
    // if above failed, kernel also passes path to main executable in apple param
    const char* mainPath = this->appleParam("executable_path");

    // if kernel arg is missing, fallback to argv[0]
    if ( mainPath == nullptr )
        mainPath = argv[0];

    // if path is not a full path, use cwd to transform it to a full path
    if ( mainPath[0] != '/' ) {
        // normalize someone running ./foo from the command line
        if ( (mainPath[0] == '.') && (mainPath[1] == '/') ) {
            mainPath += 2;
        }
        // have relative path, use cwd to make absolute
        char buff[MAXPATHLEN];
        if ( sys.getCWD(buff) ) {
            strlcat(buff, "/", MAXPATHLEN);
            strlcat(buff, mainPath, MAXPATHLEN);
            mainPath = this->strdup(buff);
        }
    }

    return mainPath;
}

bool ProcessConfig::Process::usesCatalyst()
{
#if BUILDING_DYLD
    #if TARGET_OS_OSX
        #if __arm64__
            // on Apple Silicon macs, iOS apps and Catalyst apps use catalyst runtime
            return ( (this->platform == Platform::iOSMac) || (this->platform == Platform::iOS) );
        #else
            return (this->platform == Platform::iOSMac);
        #endif
    #else
        return false;
    #endif
#else
    // FIXME: may need a way to fake iOS-apps-on-Mac for unit tests
    return ( this->platform == Platform::iOSMac );
#endif
}


uint32_t ProcessConfig::Process::findVersionSetEquivalent(dyld3::Platform versionPlatform, uint32_t version) const {
    uint32_t candidateVersion = 0;
    uint32_t candidateVersionEquivalent = 0;
    uint32_t newVersionSetVersion = 0;
    for (const auto& i : dyld3::sVersionMap) {
        switch (MachOFile::basePlatform(versionPlatform)) {
            case dyld3::Platform::macOS:    newVersionSetVersion = i.macos; break;
            case dyld3::Platform::iOS:      newVersionSetVersion = i.ios; break;
            case dyld3::Platform::watchOS:  newVersionSetVersion = i.watchos; break;
            case dyld3::Platform::tvOS:     newVersionSetVersion = i.tvos; break;
            case dyld3::Platform::bridgeOS: newVersionSetVersion = i.bridgeos; break;
            default: newVersionSetVersion = 0xffffffff; // If we do not know about the platform it is newer than everything
        }
        if (newVersionSetVersion > version) { break; }
        candidateVersion = newVersionSetVersion;
        candidateVersionEquivalent = i.set;
    }

    if (newVersionSetVersion == 0xffffffff && candidateVersion == 0) {
        candidateVersionEquivalent = newVersionSetVersion;
    }

    return candidateVersionEquivalent;
};


Platform ProcessConfig::Process::getMainPlatform()
{
    // extract platform from main executable
    this->mainExecutableSDKVersion   = 0;
    this->mainExecutableMinOSVersion = 0;
    __block Platform result = Platform::unknown;
    mainExecutable->forEachSupportedPlatform(^(Platform plat, uint32_t minOS, uint32_t sdk) {
        result = plat;
        this->mainExecutableSDKVersion   = sdk;
        this->mainExecutableMinOSVersion = minOS;
    });

    // platform overrides only applicable on macOS, and can only force to 6 or 2
    if ( result == dyld3::Platform::macOS ) {
        if ( const char* forcedPlatform = this->environ("DYLD_FORCE_PLATFORM") ) {
            if ( mainExecutable->allowsAlternatePlatform() ) {
                if ( strncmp(forcedPlatform, "6", 1) == 0 ) {
                    result = dyld3::Platform::iOSMac;
                }
                else if ( (strncmp(forcedPlatform, "2", 1) == 0) && (strcmp(mainExecutable->archName(), "arm64") == 0) ) {
                    result = dyld3::Platform::iOS;
                }

                for (const dyld3::VersionSetEntry& entry : dyld3::sVersionMap) {
                    if ( entry.macos == this->mainExecutableSDKVersion ) {
                        this->mainExecutableSDKVersion = entry.ios;
                        break;
                    }
                }
                for (const dyld3::VersionSetEntry& entry : dyld3::sVersionMap) {
                    if ( entry.macos == this->mainExecutableMinOSVersion ) {
                        this->mainExecutableMinOSVersion = entry.ios;
                        break;
                    }
                }
            }
        }
    }

    this->basePlatform = MachOFile::basePlatform(result);
    this->mainExecutableSDKVersionSet = findVersionSetEquivalent(this->basePlatform, this->mainExecutableSDKVersion);
    this->mainExecutableMinOSVersionSet = findVersionSetEquivalent(this->basePlatform, this->mainExecutableMinOSVersion);

    return result;
}


const GradedArchs* ProcessConfig::Process::getMainArchs(SyscallDelegate& sys)
{
    bool keysOff = false;
#if BUILDING_CLOSURE_UTIL
    // In closure util, just assume we want to allow arm64 binaries to get closures built
    // against arm64e shared caches
    if ( strcmp(mainExecutable->archName(), "arm64e") == 0 )
        keysOff = true;
#else
    // Check and see if kernel disabled JOP pointer signing (which lets us load plain arm64 binaries)
    if ( const char* disableStr = this->appleParam("ptrauth_disabled") ) {
        if ( strcmp(disableStr, "1") == 0 )
            keysOff = true;
    }
    else {
        // needed until kernel passes ptrauth_disabled for arm64 main executables
        if ( strcmp(mainExecutable->archName(), "arm64") == 0 )
            keysOff = true;
    }
#endif
    return &sys.getGradedArchs(mainExecutable->archName(), keysOff);
}


//
// MARK: --- Security methods ---
//

ProcessConfig::Security::Security(Process& process, SyscallDelegate& syscall)
{
    this->internalInstall           = syscall.internalInstall();  // Note: must be set up before calling getAMFI()
    this->skipMain                  = this->internalInstall && process.environ("DYLD_SKIP_MAIN");

    const uint64_t amfiFlags = getAMFI(process, syscall);
    this->allowAtPaths              = (amfiFlags & AMFI_DYLD_OUTPUT_ALLOW_AT_PATH);
    this->allowEnvVarsPrint         = (amfiFlags & AMFI_DYLD_OUTPUT_ALLOW_PRINT_VARS);
    this->allowEnvVarsPath          = (amfiFlags & AMFI_DYLD_OUTPUT_ALLOW_PATH_VARS);
    this->allowEnvVarsSharedCache   = (amfiFlags & AMFI_DYLD_OUTPUT_ALLOW_CUSTOM_SHARED_CACHE);
    this->allowClassicFallbackPaths = (amfiFlags & AMFI_DYLD_OUTPUT_ALLOW_FALLBACK_PATHS);
    this->allowInsertFailures       = (amfiFlags & AMFI_DYLD_OUTPUT_ALLOW_FAILED_LIBRARY_INSERTION);
    this->allowInterposing          = (amfiFlags & AMFI_DYLD_OUTPUT_ALLOW_LIBRARY_INTERPOSING);
#if TARGET_OS_SIMULATOR
    this->allowInsertFailures       = true; // FIXME: amfi is returning the wrong value for simulators <rdar://74025454>
#endif

    // env vars are only pruned on macOS
    switch ( process.platform ) {
        case dyld3::Platform::macOS:
        case dyld3::Platform::iOSMac:
        case dyld3::Platform::driverKit:
            break;
        default:
            return;
    }

    // env vars are only pruned when process is restricted
    if ( this->allowEnvVarsPrint || this->allowEnvVarsPath || this->allowEnvVarsSharedCache )
        return;

    this->pruneEnvVars(process);
}

uint64_t ProcessConfig::Security::getAMFI(const Process& proc, SyscallDelegate& sys)
{
    uint32_t fpTextOffset;
    uint32_t fpSize;
    uint64_t amfiFlags = sys.amfiFlags(proc.mainExecutable->isRestricted(), proc.mainExecutable->isFairPlayEncrypted(fpTextOffset, fpSize));

    bool testMode = proc.commPage.testMode;
#if !BUILDING_DYLD
    // during unit tests, commPage not set up yet, so peak ahead
    if ( const char* bootFlags = proc.appleParam("dyld_flags") ) {
        testMode = ((hexToUInt64(bootFlags, nullptr) & 0x02) != 0);
    }
#endif

    // let DYLD_AMFI_FAKE override actual AMFI flags, but only on internalInstalls with boot-arg set
    if ( const char* amfiFake = proc.environ("DYLD_AMFI_FAKE") ) {
        //console("env DYLD_AMFI_FAKE set, boot-args dyld_flags=%s\n", this->getAppleParam("dyld_flags"));
        if ( !testMode ) {
            //console("env DYLD_AMFI_FAKE ignored because boot-args dyld_flags=2 is missing (%s)\n", this->getAppleParam("dyld_flags"));
        }
        else if ( !this->internalInstall ) {
            //console("env DYLD_AMFI_FAKE ignored because not running on an Internal install\n");
        }
        else {
            amfiFlags = hexToUInt64(amfiFake, nullptr);
            //console("env DYLD_AMFI_FAKE parsed as 0x%08llX\n", amfiFlags);
       }
    }
    return amfiFlags;
}

void ProcessConfig::Security::pruneEnvVars(Process& proc)
{
    //
    // For security, setuid programs ignore DYLD_* environment variables.
    // Additionally, the DYLD_* enviroment variables are removed
    // from the environment, so that any child processes doesn't see them.
    //
    // delete all DYLD_* environment variables
    int          removedCount = 0;
    const char** d            = (const char**)proc.envp;
    for ( const char* const* s = proc.envp; *s != NULL; s++ ) {
        if ( strncmp(*s, "DYLD_", 5) != 0 ) {
            *d++ = *s;
        }
        else {
            ++removedCount;
        }
    }
    *d++ = NULL;
    // slide apple parameters
    if ( removedCount > 0 ) {
        proc.apple = d;
        do {
            *d = d[removedCount];
        } while ( *d++ != NULL );
        for ( int i = 0; i < removedCount; ++i )
            *d++ = NULL;
    }
}


//
// MARK: --- Logging methods ---
//

ProcessConfig::Logging::Logging(const Process& process, const Security& security, SyscallDelegate& syscall)
{
    this->segments       = security.allowEnvVarsPrint && process.environ("DYLD_PRINT_SEGMENTS");
    this->libraries      = security.allowEnvVarsPrint && process.environ("DYLD_PRINT_LIBRARIES");
    this->fixups         = security.allowEnvVarsPrint && process.environ("DYLD_PRINT_BINDINGS");
    this->initializers   = security.allowEnvVarsPrint && process.environ("DYLD_PRINT_INITIALIZERS");
    this->apis           = security.allowEnvVarsPrint && process.environ("DYLD_PRINT_APIS");
    this->notifications  = security.allowEnvVarsPrint && process.environ("DYLD_PRINT_NOTIFICATIONS");
    this->interposing    = security.allowEnvVarsPrint && process.environ("DYLD_PRINT_INTERPOSING");
    this->loaders        = security.allowEnvVarsPrint && process.environ("DYLD_PRINT_LOADERS");
    this->searching      = security.allowEnvVarsPrint && process.environ("DYLD_PRINT_SEARCHING");
    this->env            = security.allowEnvVarsPrint && process.environ("DYLD_PRINT_ENV");
    this->useStderr      = security.allowEnvVarsPrint && process.environ("DYLD_PRINT_TO_STDERR");
    this->descriptor     = STDERR_FILENO;
    this->useFile        = false;
    if ( security.allowEnvVarsPrint && security.allowEnvVarsSharedCache ) {
        if ( const char* path = process.environ("DYLD_PRINT_TO_FILE") ) {
            int fd = syscall.openLogFile(path);
            if ( fd != -1 ) {
                this->useFile    = true;
                this->descriptor = fd;
            }
        }
    }
}



//
// MARK: --- DyldCache methods ---
//

ProcessConfig::DyldCache::DyldCache(Process& process, const Security& security, const Logging& log, SyscallDelegate& syscall)
{
    bool forceCustomerCache = process.commPage.forceCustomerCache;
    bool forceDevCache      = process.commPage.forceDevCache;
#if BUILDING_DYLD
    // in launchd commpage is not set up yet
    if ( process.pid == 1 ) {
        if ( security.internalInstall ) {
            // default to development cache for internal installs
            forceCustomerCache = false;
            if ( const char* bootFlags = process.appleParam("dyld_flags") ) {
                // on internal installs, dyld_flags can force customer cache
                DyldCommPage cpFlags;
                *((uint32_t*)&cpFlags) = (uint32_t)hexToUInt64(bootFlags, nullptr);
                if ( cpFlags.forceCustomerCache )
                    forceCustomerCache = true;
                if ( cpFlags.forceDevCache ) {
                    forceDevCache      = true;
                    forceCustomerCache = false;
                }
            }
        }
        else {
            // customer installs always get customer dyld cache
            forceCustomerCache = true;
            forceDevCache      = false;
        }
    }
#endif

    // load dyld cache if needed
    const char*               cacheMode = process.environ("DYLD_SHARED_REGION");
#if TARGET_OS_SIMULATOR && __arm64__
    if ( cacheMode == nullptr ) {
        // A 2GB simulator app on Apple Silicon can overlay where the dyld cache is supposed to go
        // Luckily, simulators still have dylibs on disk, so we can run the process without a dyld cache
        // FIXME: Somehow get ARM64_SHARED_REGION_START = 0x180000000ULL
        if ( process.mainExecutable->intersectsRange(0x180000000ULL, 0x100000000ULL) ) {
            if ( log.segments )
                console("main executable resides where dyld cache would be, so not using a dyld cache\n");
            cacheMode = "avoid";
        }
    }
#endif
    dyld3::SharedCacheOptions opts;
    opts.cacheDirOverride         = process.environ("DYLD_SHARED_CACHE_DIR");
    opts.forcePrivate             = security.allowEnvVarsSharedCache && (cacheMode != nullptr) && (strcmp(cacheMode, "private") == 0);
    opts.useHaswell               = syscall.onHaswell();
    opts.verbose                  = log.segments;
    opts.disableASLR              = false; // FIXME
    opts.enableReadOnlyDataConst  = process.enableDataConst;
    opts.preferCustomerCache      = forceCustomerCache;
    opts.forceDevCache            = forceDevCache;
    opts.isTranslated             = process.isTranslated;
    opts.platform                 = process.platform;
    this->addr                    = nullptr;
    this->slide                   = 0;
    this->path                    = nullptr;
    this->objCCacheInfo           = nullptr;
    this->swiftCacheInfo          = nullptr;
    this->platform                = Platform::unknown;
    this->osVersion               = 0;
    this->dylibCount              = 0;
    if ( (cacheMode == nullptr) || (strcmp(cacheMode, "avoid") != 0) ) {
        dyld3::SharedCacheLoadInfo loadInfo;
        syscall.getDyldCache(opts, loadInfo);
        if ( loadInfo.loadAddress != nullptr ) {
            this->addr      = loadInfo.loadAddress;
            this->slide     = loadInfo.slide;
            this->path      = process.strdup(loadInfo.path);
            this->objCCacheInfo  = this->addr->objcOpt();
            this->swiftCacheInfo = this->addr->swiftOpt();
            this->dylibCount     = this->addr->imagesCount();
            this->setPlatformOSVersion(process);

            // The shared cache is mapped with RO __DATA_CONST, but this
            // process might need RW
            if ( !opts.enableReadOnlyDataConst )
                makeDataConstWritable(log, syscall, true);
        }
        else {
#if BUILDING_DYLD && !TARGET_OS_SIMULATOR
            // <rdar://74102798> log all shared cache errors except no cache file
            if ( loadInfo.cacheFileFound )
                console("dyld cache '%s' not loaded: %s\n", loadInfo.path, loadInfo.errorMessage);
#endif
        }
    }
#if BUILDING_DYLD
    // in launchd we set up the dyld comm-page bits
    if ( process.pid == 1 )
#endif
        this->setupDyldCommPage(process, security, syscall);
}

bool ProcessConfig::DyldCache::uuidOfFileMatchesDyldCache(const Process& proc, const SyscallDelegate& sys, const char* dylibPath) const
{
    // get UUID of dylib in cache
    if ( const dyld3::MachOFile* cacheMF = this->addr->getImageFromPath(dylibPath) ) {
        uuid_t cacheUUID;
        if ( !cacheMF->getUuid(cacheUUID) )
            return false;

        // get UUID of file on disk
        uuid_t              diskUUID;
        uint8_t*            diskUUIDPtr   = diskUUID; // work around compiler bug with arrays and __block
        __block bool        diskUuidFound = false;
        __block Diagnostics diag;
        sys.withReadOnlyMappedFile(diag, dylibPath, false, ^(const void* mapping, size_t mappedSize, bool isOSBinary, const FileID& fileID, const char* canonicalPath) {
            if ( const MachOFile* diskMF = MachOFile::compatibleSlice(diag, mapping, mappedSize, dylibPath, proc.platform, isOSBinary, *proc.archs) ) {
                diskUuidFound = diskMF->getUuid(diskUUIDPtr);
            }
        });
        if ( !diskUuidFound )
            return false;

        return (::memcmp(diskUUID, cacheUUID, sizeof(uuid_t)) == 0);
    }
    return false;
}

void ProcessConfig::DyldCache::setPlatformOSVersion(const Process& proc)
{
    // new caches have OS version recorded
    if ( addr->header.mappingOffset >= 0x170 ) {
        // decide if process is using main platform or alternate platform
        if ( proc.platform == (Platform)addr->header.platform ) {
            this->platform  = (Platform)addr->header.platform;
            this->osVersion = addr->header.osVersion;
        }
        else {
            this->platform  = (Platform)addr->header.altPlatform;
            this->osVersion = addr->header.altOsVersion;;
        }
    }
    else {
        // for older caches, need to find and inspect libdyld.dylib
        const char* libdyldPath = (proc.platform == Platform::driverKit) ? "/System/DriverKit/usr/lib/system/libdyld.dylib" : "/usr/lib/system/libdyld.dylib";
        if ( const dyld3::MachOFile* libdyldMF = this->addr->getImageFromPath(libdyldPath) ) {
            libdyldMF->forEachSupportedPlatform(^(Platform aPlatform, uint32_t minOS, uint32_t sdk) {
                if ( aPlatform == proc.platform ) {
                    this->platform  = aPlatform;
                    this->osVersion = minOS;
                }
                else if ( (aPlatform == Platform::iOSMac) && proc.catalystRuntime ) {
                    // support iPad apps running on Apple Silicon
                    this->platform  = aPlatform;
                    this->osVersion = minOS;
                }
            });
        }
        else {
            console("initializeCachePlatformOSVersion(): libdyld.dylib not found for OS version info\n");
        }
     }
}

void ProcessConfig::DyldCache::setupDyldCommPage(Process& proc, const Security& sec, SyscallDelegate& sys)
{
    DyldCommPage cpFlags;
#if !TARGET_OS_SIMULATOR
    // in launchd we compute the comm-page flags we want and set them for other processes to read
    cpFlags.bootVolumeWritable = sys.bootVolumeWritable();
    if ( const char* bootFlags = proc.appleParam("dyld_flags") ) {
        // low 32-bits of comm page comes from dyld_flags boot-arg
        *((uint32_t*)&cpFlags) = (uint32_t)hexToUInt64(bootFlags, nullptr);
        if ( !sec.internalInstall ) {
            cpFlags.forceCustomerCache = true;
            cpFlags.testMode           = false;
            cpFlags.forceDevCache      = false;
            cpFlags.bootVolumeWritable = false;
        }
    }
#endif
#if TARGET_OS_OSX
    // on macOS, three dylibs under libsystem are on disk but may need to be ignored
    if ( this->addr != nullptr ) {
        cpFlags.libKernelRoot   = !this->uuidOfFileMatchesDyldCache(proc, sys, "/usr/lib/system/libsystem_kernel.dylib");
        cpFlags.libPlatformRoot = !this->uuidOfFileMatchesDyldCache(proc, sys, "/usr/lib/system/libsystem_platform.dylib");
        cpFlags.libPthreadRoot  = !this->uuidOfFileMatchesDyldCache(proc, sys, "/usr/lib/system/libsystem_pthread.dylib");
    }
#endif
    sys.setDyldCommPageFlags(cpFlags);
    proc.commPage = cpFlags;
}

bool ProcessConfig::DyldCache::indexOfPath(const char* dylibPath, uint32_t& dylibIndex) const
{
    if ( this->addr == nullptr )
        return false;

    return this->addr->hasImagePath(dylibPath, dylibIndex);
}



void ProcessConfig::DyldCache::makeDataConstWritable(const Logging& lg, const SyscallDelegate& sys, bool writable) const
{
    const uint32_t perms = (writable ? VM_PROT_WRITE | VM_PROT_READ | VM_PROT_COPY : VM_PROT_READ);
    addr->forEachCache(^(const DyldSharedCache *cache, bool& stopCache) {
        cache->forEachRegion(^(const void*, uint64_t vmAddr, uint64_t size, uint32_t initProt, uint32_t maxProt, uint64_t flags, bool& stopRegion) {
            void* content = (void*)(vmAddr + slide);
            if ( flags & DYLD_CACHE_MAPPING_CONST_DATA ) {
                if ( lg.segments )
                    console("marking shared cache range 0x%x permissions: 0x%09lX -> 0x%09lX\n", perms, (long)content, (long)content + (long)size);
                kern_return_t result = sys.vm_protect(mach_task_self(), (vm_address_t)content, (vm_size_t)size, false, perms);
                if ( result != KERN_SUCCESS ) {
                    if ( lg.segments )
                        console("failed to mprotect shared cache due to: %d\n", result);
                }
            }
        });
    });
}
//
// MARK: --- PathOverrides methods ---
//

ProcessConfig::PathOverrides::PathOverrides(const Process& process, const Security& security, const Logging& log, const DyldCache& cache, SyscallDelegate& syscall)
{
    // set fallback path mode
    _fallbackPathMode = security.allowClassicFallbackPaths ? FallbackPathMode::classic : FallbackPathMode::restricted;

    // process DYLD_* env variables if allowed
    if ( security.allowEnvVarsPath ) {
        char crashMsg[2048];
        strlcpy(crashMsg, "dyld4 config: ", sizeof(crashMsg));
        for (const char* const* p = process.envp; *p != nullptr; ++p) {
            this->addEnvVar(process, security, *p, false, crashMsg);
        }
        if ( strlen(crashMsg) > 15 ) {
            // if there is a crash, have DYLD_ env vars show up in crash log
            CRSetCrashLogMessage(process.strdup(crashMsg));
        }
    }

    // process LC_DYLD_ENVIRONMENT variables
    process.mainExecutable->forDyldEnv(^(const char* keyEqualValue, bool& stop) {
        this->addEnvVar(process, security, keyEqualValue, true, nullptr);
    });

    // process DYLD_VERSIONED_* env vars if allowed
    if ( security.allowEnvVarsPath )
        this->processVersionedPaths(process, syscall, cache, process.platform, *process.archs);
}


void ProcessConfig::PathOverrides::checkVersionedPath(const Process& proc, const char* path, SyscallDelegate& sys, const DyldCache& cache, Platform platform, const GradedArchs& archs)
{
    static bool verbose = false;
    if (verbose) console("checkVersionedPath(%s)\n", path);
    uint32_t foundDylibVersion;
    char     foundDylibTargetOverridePath[PATH_MAX];
    if ( sys.getDylibInfo(path, platform, archs, foundDylibVersion, foundDylibTargetOverridePath) ) {
        if (verbose) console("   dylib vers=0x%08X (%s)\n", foundDylibVersion, path);
        uint32_t targetDylibVersion;
        uint32_t dylibIndex;
        char     targetInstallName[PATH_MAX];
        if (verbose) console("   look for OS dylib at %s\n", foundDylibTargetOverridePath);
        bool foundOSdylib = false;
        if ( sys.getDylibInfo(foundDylibTargetOverridePath, platform, archs, targetDylibVersion, targetInstallName) ) {
            foundOSdylib = true;
        }
        else if ( cache.indexOfPath(foundDylibTargetOverridePath, dylibIndex) )  {
            uint64_t unusedMTime = 0;
            uint64_t unusedINode = 0;
            const MachOAnalyzer* cacheMA = (MachOAnalyzer*)cache.addr->getIndexedImageEntry(dylibIndex, unusedMTime, unusedINode);
            const char* dylibInstallName;
            uint32_t    compatVersion;
            if ( cacheMA->getDylibInstallName(&dylibInstallName, &compatVersion, &targetDylibVersion) ) {
                strlcpy(targetInstallName, dylibInstallName, PATH_MAX);
                foundOSdylib = true;
            }
        }
        if ( foundOSdylib ) {
            if (verbose) console("   os dylib vers=0x%08X (%s)\n", targetDylibVersion, foundDylibTargetOverridePath);
            if ( foundDylibVersion > targetDylibVersion ) {
                // check if there already is an override path
                bool add = true;
                for (DylibOverride* existing=_versionedOverrides; existing != nullptr; existing=existing->next) {
                    if ( strcmp(existing->installName, targetInstallName) == 0 ) {
                        add = false; // already have an entry, don't add another
                        uint32_t previousDylibVersion;
                        char     previousInstallName[PATH_MAX];
                        if ( sys.getDylibInfo(existing->overridePath, platform, archs, previousDylibVersion, previousInstallName) ) {
                            // if already found an override and its version is greater that this one, don't add this one
                            if ( foundDylibVersion > previousDylibVersion ) {
                                existing->overridePath = proc.strdup(path);
                                if (verbose) console("  override: alter to %s with: %s\n", targetInstallName, path);
                            }
                        }
                        break;
                    }
                }
                if ( add ) {
                    //console("  override: %s with: %s\n", installName, overridePath);
                    addPathOverride(proc, targetInstallName, path);
                }
            }
        }
        else {
            // <rdar://problem/53215116> DYLD_VERSIONED_LIBRARY_PATH fails to load a dylib if it does not also exist at the system install path
            addPathOverride(proc, foundDylibTargetOverridePath, path);
        }
    }
}

void ProcessConfig::PathOverrides::addPathOverride(const Process& proc, const char* installName, const char* overridePath)
{
    DylibOverride* newElement = (DylibOverride*)proc.roalloc(sizeof(DylibOverride));
    newElement->next         = nullptr;
    newElement->installName  = proc.strdup(installName);
    newElement->overridePath = proc.strdup(overridePath);
    // add to end of linked list
    if ( _versionedOverrides != nullptr )  {
        DylibOverride* last = _versionedOverrides;
        while ( last->next != nullptr )
            last = last->next;
        last->next = newElement;
    }
    else {
        _versionedOverrides = newElement;
    }
}

void ProcessConfig::PathOverrides::processVersionedPaths(const Process& proc, SyscallDelegate& sys, const DyldCache& cache, Platform platform, const GradedArchs& archs)
{
    // check DYLD_VERSIONED_LIBRARY_PATH for dylib overrides
    if ( (_versionedDylibPathsEnv != nullptr) || (_versionedDylibPathExeLC != nullptr) ) {
        forEachInColonList(_versionedDylibPathsEnv, _versionedDylibPathExeLC, ^(const char* searchDir, bool& stop) {
            sys.forEachInDirectory(searchDir, false, ^(const char* pathInDir) {
                this->checkVersionedPath(proc, pathInDir, sys, cache, platform, archs);
            });
        });
    }
    // check DYLD_VERSIONED_FRAMEWORK_PATH for framework overrides
    if ( (_versionedFrameworkPathsEnv != nullptr) || (_versionedFrameworkPathExeLC != nullptr) ) {
        forEachInColonList(_versionedFrameworkPathsEnv, _versionedFrameworkPathExeLC, ^(const char* searchDir, bool& stop) {
            sys.forEachInDirectory(searchDir, true, ^(const char* pathInDir) {
                // ignore paths that don't end in ".framework"
                size_t pathInDirLen = strlen(pathInDir);
                if ( (pathInDirLen < 10) || (strcmp(&pathInDir[pathInDirLen-10], ".framework") != 0)  )
                    return;
                // make ..path/Foo.framework/Foo
                char possibleFramework[PATH_MAX];
                strlcpy(possibleFramework, pathInDir, PATH_MAX);
                strlcat(possibleFramework, strrchr(pathInDir, '/'), PATH_MAX);
                *strrchr(possibleFramework, '.') = '\0';
                this->checkVersionedPath(proc, possibleFramework, sys, cache, platform, archs);
            });
        });
    }
}


void ProcessConfig::PathOverrides::forEachInsertedDylib(void (^handler)(const char* dylibPath, bool& stop)) const
{
    if ( _insertedDylibs != nullptr && _insertedDylibs[0] != '\0' ) {
        forEachInColonList(_insertedDylibs, nullptr, ^(const char* path, bool& stop) {
            handler(path, stop);
        });
    }
}

void ProcessConfig::PathOverrides::handleEnvVar(const char* key, const char* value, void (^handler)(const char* envVar)) const
{
    if ( value == nullptr )
        return;
    size_t allocSize = strlen(key) + strlen(value) + 2;
    char buffer[allocSize];
    strlcpy(buffer, key, allocSize);
    strlcat(buffer, "=", allocSize);
    strlcat(buffer, value, allocSize);
    handler(buffer);
}

// Note, this method only returns variables set on the environment, not those from the load command
void ProcessConfig::PathOverrides::forEachEnvVar(void (^handler)(const char* envVar)) const
{
    handleEnvVar("DYLD_LIBRARY_PATH",             _dylibPathOverridesEnv,        handler);
    handleEnvVar("DYLD_FRAMEWORK_PATH",           _frameworkPathOverridesEnv,    handler);
    handleEnvVar("DYLD_FALLBACK_FRAMEWORK_PATH",  _frameworkPathFallbacksEnv,    handler);
    handleEnvVar("DYLD_FALLBACK_LIBRARY_PATH",    _dylibPathFallbacksEnv,        handler);
    handleEnvVar("DYLD_VERSIONED_FRAMEWORK_PATH", _versionedFrameworkPathsEnv,   handler);
    handleEnvVar("DYLD_VERSIONED_LIBRARY_PATH",   _versionedDylibPathsEnv,       handler);
    handleEnvVar("DYLD_INSERT_LIBRARIES",         _insertedDylibs,               handler);
    handleEnvVar("DYLD_IMAGE_SUFFIX",             _imageSuffix,                  handler);
    handleEnvVar("DYLD_ROOT_PATH",                _simRootPath,                  handler);
}

// Note, this method only returns variables set in the main executable's load command, not those from the environment
void ProcessConfig::PathOverrides::forEachExecutableEnvVar(void (^handler)(const char* envVar)) const
{
    handleEnvVar("DYLD_LIBRARY_PATH",             _dylibPathOverridesExeLC,        handler);
    handleEnvVar("DYLD_FRAMEWORK_PATH",           _frameworkPathOverridesExeLC,    handler);
    handleEnvVar("DYLD_FALLBACK_FRAMEWORK_PATH",  _frameworkPathFallbacksExeLC,    handler);
    handleEnvVar("DYLD_FALLBACK_LIBRARY_PATH",    _dylibPathFallbacksExeLC,        handler);
    handleEnvVar("DYLD_VERSIONED_FRAMEWORK_PATH", _versionedFrameworkPathExeLC,    handler);
    handleEnvVar("DYLD_VERSIONED_LIBRARY_PATH",   _versionedDylibPathExeLC,        handler);
}

void ProcessConfig::PathOverrides::setString(const Process& proc, const char*& var, const char* value)
{
    // ivar not set, just set to copy of string
    if ( var == nullptr ) {
        var = proc.strdup(value);
        return;
    }
    // ivar already in use, build new appended string
    char tmp[strlen(var)+strlen(value)+2];
    strcpy(tmp, var);
    strcat(tmp, ":");
    strcat(tmp, value);
    var = proc.strdup(tmp);
}

void ProcessConfig::PathOverrides::addEnvVar(const Process& proc, const Security& sec, const char* keyEqualsValue, bool isLC_DYLD_ENV, char* crashMsg)
{
    // We have to make a copy of the env vars because the dyld semantics
    // is that the env vars are only looked at once at launch.
    // That is, using setenv() at runtime does not change dyld behavior.
    if ( const char* equals = ::strchr(keyEqualsValue, '=') ) {
        const char* value = equals+1;
        if ( isLC_DYLD_ENV && (strchr(value, '@') != nullptr) ) {
            char buffer[PATH_MAX];
            char* expandedPaths = buffer;
            __block bool needColon = false;
            buffer[0] = '\0';
            forEachInColonList(value, nullptr,  ^(const char* aValue, bool& innerStop) {
                if ( !sec.allowAtPaths && (aValue[0] == '@') )
                    return;
                if ( needColon )
                    ::strlcat(expandedPaths, ":", PATH_MAX);
                if ( strncmp(aValue, "@executable_path/", 17) == 0 ) {
                    ::strlcat(expandedPaths, proc.mainExecutablePath, PATH_MAX);
                    if ( char* lastSlash = ::strrchr(expandedPaths, '/') ) {
                        ::strcpy(lastSlash+1, &aValue[17]);
                        needColon = true;
                    }
                }
                else if ( strncmp(aValue, "@loader_path/", 13) == 0 ) {
                    ::strlcat(expandedPaths, proc.mainExecutablePath, PATH_MAX);
                    if ( char* lastSlash = ::strrchr(expandedPaths, '/') ) {
                        ::strcpy(lastSlash+1, &aValue[13]);
                         needColon = true;
                   }
                }
                else {
                    ::strlcpy(expandedPaths, proc.mainExecutablePath, PATH_MAX);
                    needColon = true;
                }
            });
            value = proc.strdup(expandedPaths);
        }
        bool addToCrashMsg = false;
        if ( strncmp(keyEqualsValue, "DYLD_LIBRARY_PATH", 17) == 0 ) {
            setString(proc, isLC_DYLD_ENV ? _dylibPathOverridesExeLC : _dylibPathOverridesEnv, value);
            addToCrashMsg = true;
        }
        else if ( strncmp(keyEqualsValue, "DYLD_FRAMEWORK_PATH", 19) == 0 ) {
            setString(proc, isLC_DYLD_ENV ? _frameworkPathOverridesExeLC : _frameworkPathOverridesEnv, value);
            addToCrashMsg = true;
        }
        else if ( strncmp(keyEqualsValue, "DYLD_FALLBACK_FRAMEWORK_PATH", 28) == 0 ) {
            setString(proc, isLC_DYLD_ENV ? _frameworkPathFallbacksExeLC : _frameworkPathFallbacksEnv, value);
        }
        else if ( strncmp(keyEqualsValue, "DYLD_FALLBACK_LIBRARY_PATH", 26) == 0 ) {
            setString(proc, isLC_DYLD_ENV ? _dylibPathFallbacksExeLC : _dylibPathFallbacksEnv, value);
        }
        else if ( strncmp(keyEqualsValue, "DYLD_VERSIONED_FRAMEWORK_PATH", 28) == 0 ) {
            setString(proc, isLC_DYLD_ENV ? _versionedFrameworkPathExeLC : _versionedFrameworkPathsEnv, value);
        }
        else if ( strncmp(keyEqualsValue, "DYLD_VERSIONED_LIBRARY_PATH", 26) == 0 ) {
            setString(proc, isLC_DYLD_ENV ? _versionedDylibPathExeLC : _versionedDylibPathsEnv, value);
        }
        else if ( strncmp(keyEqualsValue, "DYLD_INSERT_LIBRARIES", 21) == 0 ) {
            setString(proc, _insertedDylibs, value);
            if ( _insertedDylibs[0] != '\0' ) {
                _insertedDylibCount = 1;
                for (const char* s=_insertedDylibs; *s != '\0'; ++s) {
                    if ( *s == ':' )
                        _insertedDylibCount++;
                }
            }
            addToCrashMsg = true;
        }
        else if ( strncmp(keyEqualsValue, "DYLD_IMAGE_SUFFIX", 17) == 0 ) {
            setString(proc, _imageSuffix, value);
            addToCrashMsg = true;
        }
        else if ( (strncmp(keyEqualsValue, "DYLD_ROOT_PATH", 14) == 0) && MachOFile::isSimulatorPlatform(proc.platform) ) {
            setString(proc, _simRootPath, value);
             addToCrashMsg = true;
       }
        if ( addToCrashMsg && (crashMsg != nullptr) ) {
            strlcat(crashMsg, keyEqualsValue, 2048);
            strlcat(crashMsg, " ", 2048);
        }
    }
}

void ProcessConfig::PathOverrides::forEachInColonList(const char* list1, const char* list2, void (^handler)(const char* path, bool& stop))
{
    for (const char* list : { list1, list2 }) {
        if (list == nullptr)
            continue;
        char buffer[strlen(list)+1];
        const char* t = list;
        bool stop = false;
        for (const char* s=list; *s != '\0'; ++s) {
            if (*s != ':')
                continue;
            size_t len = s - t;
            memcpy(buffer, t, len);
            buffer[len] = '\0';
            handler(buffer, stop);
            if ( stop )
                return;
            t = s+1;
        }
        handler(t, stop);
        if (stop)
            return;
    }
}

void ProcessConfig::PathOverrides::forEachDylibFallback(Platform platform, bool disableCustom, void (^handler)(const char* fallbackDir, Type type, bool& stop)) const
{
    __block bool stop = false;
    if ( !disableCustom && ((_dylibPathFallbacksEnv != nullptr) || (_dylibPathFallbacksExeLC != nullptr)) ) {
        forEachInColonList(_dylibPathFallbacksEnv, _dylibPathFallbacksExeLC, ^(const char* pth, bool& innerStop) {
            handler(pth, Type::customFallback, innerStop);
            if ( innerStop )
                stop = true;
        });
    }
    else {
        switch ( platform ) {
            case Platform::macOS:
                switch ( _fallbackPathMode ) {
                    case FallbackPathMode::classic:
                        // "$HOME/lib"
                        handler("/usr/local/lib", Type::standardFallback, stop);
                        if ( stop )
                            break;
                        [[clang::fallthrough]];
                    case FallbackPathMode::restricted:
                        handler("/usr/lib", Type::standardFallback, stop);
                        break;
                    case FallbackPathMode::none:
                        break;
                }
                break;
            case Platform::iOS:
            case Platform::watchOS:
            case Platform::tvOS:
            case Platform::bridgeOS:
            case Platform::unknown:
                if ( _fallbackPathMode != FallbackPathMode::none ) {
                    handler("/usr/local/lib", Type::standardFallback, stop);
                    if ( stop )
                        break;
                }
                // fall into /usr/lib case
                [[clang::fallthrough]];
            case Platform::iOSMac:
            case Platform::iOS_simulator:
            case Platform::watchOS_simulator:
            case Platform::tvOS_simulator:
                if ( _fallbackPathMode != FallbackPathMode::none )
                    handler("/usr/lib", Type::standardFallback, stop);
                break;
            case Platform::driverKit:
                // no fallback searching for driverkit
                break;
        }
    }
}

void ProcessConfig::PathOverrides::forEachFrameworkFallback(Platform platform, bool disableCustom, void (^handler)(const char* fallbackDir, Type type, bool& stop)) const
{
    __block bool stop = false;
    if ( !disableCustom && ((_frameworkPathFallbacksEnv != nullptr) || (_frameworkPathFallbacksExeLC != nullptr)) ) {
        forEachInColonList(_frameworkPathFallbacksEnv, _frameworkPathFallbacksExeLC, ^(const char* pth, bool& innerStop) {
            handler(pth, Type::customFallback, innerStop);
            if ( innerStop )
                stop = true;
        });
    }
    else {
        switch ( platform ) {
            case Platform::macOS:
                switch ( _fallbackPathMode ) {
                    case FallbackPathMode::classic:
                        // "$HOME/Library/Frameworks"
                        handler("/Library/Frameworks", Type::standardFallback, stop);
                        if ( stop )
                            break;
                        // "/Network/Library/Frameworks"
                        // fall thru
                        [[clang::fallthrough]];
                    case FallbackPathMode::restricted:
                        handler("/System/Library/Frameworks", Type::standardFallback, stop);
                        break;
                    case FallbackPathMode::none:
                        break;
                }
                break;
            case Platform::iOS:
            case Platform::watchOS:
            case Platform::tvOS:
            case Platform::bridgeOS:
            case Platform::iOSMac:
            case Platform::iOS_simulator:
            case Platform::watchOS_simulator:
            case Platform::tvOS_simulator:
            case Platform::unknown:
                if ( _fallbackPathMode != FallbackPathMode::none )
                    handler("/System/Library/Frameworks", Type::standardFallback, stop);
                break;
            case Platform::driverKit:
                // no fallback searching for driverkit
                break;
        }
    }
}


//
// copy path and add suffix to result
//
//  /path/foo.dylib      _debug   =>   /path/foo_debug.dylib
//  foo.dylib            _debug   =>   foo_debug.dylib
//  foo                  _debug   =>   foo_debug
//  /path/bar            _debug   =>   /path/bar_debug
//  /path/bar.A.dylib    _debug   =>   /path/bar.A_debug.dylib
//
void ProcessConfig::PathOverrides::addSuffix(const char* path, const char* suffix, char* result) const
{
    strcpy(result, path);

    // find last slash
    char* start = strrchr(result, '/');
    if ( start != NULL )
        start++;
    else
        start = result;

    // find last dot after last slash
    char* dot = strrchr(start, '.');
    if ( dot != NULL ) {
        strcpy(dot, suffix);
        strcat(&dot[strlen(suffix)], &path[dot-result]);
    }
    else {
        strcat(result, suffix);
    }
}

void ProcessConfig::PathOverrides::forEachImageSuffix(const char* path, Type type, bool& stop,
                                                      void (^handler)(const char* possiblePath, Type type, bool& stop)) const
{
    if ( _imageSuffix == nullptr ) {
        handler(path, type, stop);
    }
    else {
        forEachInColonList(_imageSuffix, nullptr, ^(const char* suffix, bool& innerStop) {
            char npath[strlen(path)+strlen(suffix)+8];
            addSuffix(path, suffix, npath);
            handler(npath, Type::suffixOverride, innerStop);
            if ( innerStop )
                stop = true;
        });
        if ( !stop )
            handler(path, type, stop);
    }
}

void ProcessConfig::PathOverrides::forEachPathVariant(const char* initialPath, Platform platform, bool disableCustomFallbacks, bool& stop,
                                                      void (^handler)(const char* possiblePath, Type type, bool& stop)) const
{
    // check for overrides
    const char* frameworkPartialPath = getFrameworkPartialPath(initialPath);
    if ( frameworkPartialPath != nullptr ) {
        const size_t frameworkPartialPathLen = strlen(frameworkPartialPath);
        // look at each DYLD_FRAMEWORK_PATH directory
        if ( (_frameworkPathOverridesEnv != nullptr) || (_frameworkPathOverridesExeLC != nullptr) ) {
            forEachInColonList(_frameworkPathOverridesEnv, _frameworkPathOverridesExeLC, ^(const char* frDir, bool& innerStop) {
                char npath[strlen(frDir)+frameworkPartialPathLen+8];
                strcpy(npath, frDir);
                strcat(npath, "/");
                strcat(npath, frameworkPartialPath);
                forEachImageSuffix(npath, Type::pathDirOverride, innerStop, handler);
                if ( innerStop )
                    stop = true;
            });
        }
    }
    else {
        const char* libraryLeafName = getLibraryLeafName(initialPath);
        const size_t libraryLeafNameLen = strlen(libraryLeafName);
        // look at each DYLD_LIBRARY_PATH directory
        if ( (_dylibPathOverridesEnv != nullptr) || (_dylibPathOverridesExeLC != nullptr) ) {
            forEachInColonList(_dylibPathOverridesEnv, _dylibPathOverridesExeLC, ^(const char* libDir, bool& innerStop) {
                char npath[strlen(libDir)+libraryLeafNameLen+8];
                strcpy(npath, libDir);
                strcat(npath, "/");
                strcat(npath, libraryLeafName);
                forEachImageSuffix(npath, Type::pathDirOverride, innerStop, handler);
                if ( innerStop )
                    stop = true;
            });
        }
    }
    if ( stop )
        return;

    // check for versioned_path overrides
    for (DylibOverride* replacement=_versionedOverrides; replacement != nullptr; replacement=replacement->next) {
        if ( strcmp(replacement->installName, initialPath) == 0 ) {
            handler(replacement->overridePath, Type::versionedOverride, stop);
            // note: always stop searching when versioned override is found
            return;
       }
    }

    // paths staring with @ are never valid for finding in iOSSupport or simulator
    if ( initialPath[0] != '@' ) {

        // try rootpath
        bool searchiOSSupport = (platform == Platform::iOSMac);
    #if (TARGET_OS_OSX && TARGET_CPU_ARM64)
        if ( platform == Platform::iOS ) {
            searchiOSSupport = true;
            // <rdar://problem/58959974> some old Almond apps reference old WebKit location
            if ( strcmp(initialPath, "/System/Library/PrivateFrameworks/WebKit.framework/WebKit") == 0 )
                initialPath = "/System/Library/Frameworks/WebKit.framework/WebKit";
        }
    #endif

        // try looking in Catalyst support dir
        if ( searchiOSSupport && (strncmp(initialPath, "/System/iOSSupport/", 19) != 0) ) {
            char rtpath[strlen("/System/iOSSupport")+strlen(initialPath)+8];
            strcpy(rtpath, "/System/iOSSupport");
            strcat(rtpath, initialPath);
            forEachImageSuffix(rtpath, Type::catalystPrefix, stop, handler);
            if ( stop )
                return;
        }
    #if TARGET_OS_SIMULATOR
        if ( _simRootPath != nullptr ) {
            // try simulator prefix
            char rtpath[strlen(_simRootPath)+strlen(initialPath)+8];
            strcpy(rtpath, _simRootPath);
            strcat(rtpath, initialPath);
            forEachImageSuffix(rtpath, Type::simulatorPrefix, stop, handler);
            if ( stop )
                return;
        }
    #endif
    }

    // try original path
    forEachImageSuffix(initialPath, Type::rawPath, stop, handler);
    if ( stop )
        return;

    // check fallback paths
    if ( frameworkPartialPath != nullptr ) {
        const size_t frameworkPartialPathLen = strlen(frameworkPartialPath);
        // look at each DYLD_FALLBACK_FRAMEWORK_PATH directory
        forEachFrameworkFallback(platform, disableCustomFallbacks, ^(const char* dir, Type type, bool& innerStop) {
            char npath[strlen(dir)+frameworkPartialPathLen+8];
            strcpy(npath, dir);
            strcat(npath, "/");
            strcat(npath, frameworkPartialPath);
            // don't try original path again
            if ( strcmp(initialPath, npath) != 0 ) {
                forEachImageSuffix(npath, type, innerStop, handler);
                if ( innerStop )
                    stop = true;
            }
        });

    }
   else {
        const char* libraryLeafName = getLibraryLeafName(initialPath);
        const size_t libraryLeafNameLen = strlen(libraryLeafName);
        // look at each DYLD_FALLBACK_LIBRARY_PATH directory
        forEachDylibFallback(platform, disableCustomFallbacks, ^(const char* dir, Type type, bool& innerStop) {
            char libpath[strlen(dir)+libraryLeafNameLen+8];
            strcpy(libpath, dir);
            strcat(libpath, "/");
            strcat(libpath, libraryLeafName);
            if ( strcmp(libpath, initialPath) != 0 ) {
                forEachImageSuffix(libpath, type, innerStop, handler);
                if ( innerStop )
                    stop = true;
            }
        });
    }
}



//
// Find framework path
//
//  /path/foo.framework/foo                             =>   foo.framework/foo
//  /path/foo.framework/Versions/A/foo                  =>   foo.framework/Versions/A/foo
//  /path/foo.framework/Frameworks/bar.framework/bar    =>   bar.framework/bar
//  /path/foo.framework/Libraries/bar.dylb              =>   NULL
//  /path/foo.framework/bar                             =>   NULL
//
// Returns nullptr if not a framework path
//
const char* ProcessConfig::PathOverrides::getFrameworkPartialPath(const char* path) const
{
    const char* dirDot = strrstr(path, ".framework/");
    if ( dirDot != nullptr ) {
        const char* dirStart = dirDot;
        for ( ; dirStart >= path; --dirStart) {
            if ( (*dirStart == '/') || (dirStart == path) ) {
                const char* frameworkStart = &dirStart[1];
                if ( dirStart == path )
                    --frameworkStart;
                size_t len = dirDot - frameworkStart;
                char framework[len+1];
                strncpy(framework, frameworkStart, len);
                framework[len] = '\0';
                const char* leaf = strrchr(path, '/');
                if ( leaf != nullptr ) {
                    if ( strcmp(framework, &leaf[1]) == 0 ) {
                        return frameworkStart;
                    }
                    if (  _imageSuffix != nullptr ) {
                        // some debug frameworks have install names that end in _debug
                        if ( strncmp(framework, &leaf[1], len) == 0 ) {
                            if ( strcmp( _imageSuffix, &leaf[len+1]) == 0 )
                                return frameworkStart;
                        }
                    }
                }
            }
        }
    }
    return nullptr;
}


const char* ProcessConfig::PathOverrides::getLibraryLeafName(const char* path)
{
    const char* start = strrchr(path, '/');
    if ( start != nullptr )
        return &start[1];
    else
        return path;
}

const char* ProcessConfig::PathOverrides::typeName(Type type)
{
    switch (type) {
        case pathDirOverride:
            return "DYLD_FRAMEWORK/LIBRARY_PATH";
        case versionedOverride:
            return "DYLD_VERSIONED_FRAMEWORK/LIBRARY_PATH";
        case suffixOverride:
            return "DYLD_IMAGE_SUFFIX";
        case catalystPrefix:
            return "Catalyst prefix";
        case simulatorPrefix:
            return "simulator prefix";
        case rawPath:
            return "original path";
        case rpathExpansion:
            return "@path expansion";
        case loaderPathExpansion:
            return "@loader_path expansion";
        case executablePathExpansion:
            return "@executable_path expanstion";
        case implictRpathExpansion:
            return "leaf name using rpath";
        case customFallback:
            return "DYLD_FRAMEWORK/LIBRARY_FALLBACK_PATH";
        case standardFallback:
            return "default fallback";
    }
    return "unknown";
}

bool ProcessConfig::PathOverrides::dontUsePrebuiltForApp() const
{
    // DYLD_LIBRARY_PATH and DYLD_FRAMEWORK_PATH disable building PrebuiltLoader for app
    if ( _dylibPathOverridesEnv || _frameworkPathOverridesEnv )
        return true;

    // DYLD_VERSIONED_LIBRARY_PATH and DYLD_VERSIONED_FRAMEWORK_PATH disable building PrebuiltLoader for app
    if ( _versionedDylibPathsEnv || _versionedFrameworkPathsEnv )
        return true;

    // DYLD_INSERT_LIBRARIES and DYLD_IMAGE_SUFFIX disable building PrebuiltLoader for app
    if ( _insertedDylibs || _imageSuffix )
        return true;

    // LC_DYLD_ENVIRONMENT VERSIONED* paths disable building PrebuiltLoader for app
    // TODO: rdar://73360795 (need a way to allow PrebuiltLoaderSets to work the VERSIONED_PATH)
    if ( _versionedDylibPathExeLC || _versionedFrameworkPathExeLC)
        return true;

    return false;
}


//
// MARK: --- ProcessConfig methods ---
//

bool ProcessConfig::simulatorFileMatchesDyldCache(const char *path) const
{
#if TARGET_OS_OSX
    // On macOS there are three dylibs under libSystem that exist for the simulator to use,
    // but we do not consider them "roots", so fileExists() returns false for them
    if ( (this->dyldCache.addr != nullptr) && (strncmp(path, "/usr/lib/system/libsystem_", 26) == 0) ) {
        const char* ending = &path[26];
        if ( strcmp(ending, "platform.dylib") == 0 ) {
            // If this was a root when launchd checked, then assume we are a root now
            if ( this->process.commPage.libPlatformRoot )
                return false;

            // If the file system is read-only, then this cannot be a root now
            if ( !this->process.commPage.bootVolumeWritable )
                return true;

            // Possibly a root, open the file and compare UUID to one in dyld cache
            return this->dyldCache.uuidOfFileMatchesDyldCache(process, syscall, path);
        }
        else if ( strcmp(ending, "pthread.dylib") == 0 ) {
            // If this was a root when launchd checked, then assume we are a root now
            if ( this->process.commPage.libPthreadRoot )
                return false;

            // If the file system is read-only, then this cannot be a root now
            if ( !this->process.commPage.bootVolumeWritable )
                return true;

            // Possibly a root, open the file and compare UUID to one in dyld cache
            return this->dyldCache.uuidOfFileMatchesDyldCache(process, syscall, path);
        }
        else if ( strcmp(ending, "kernel.dylib") == 0 ) {
            // If this was a root when launchd checked, then assume we are a root now
            if ( this->process.commPage.libKernelRoot )
                return false;

            // If the file system is read-only, then this cannot be a root now
            if ( !this->process.commPage.bootVolumeWritable )
                return true;

            // Possibly a root, open the file and compare UUID to one in dyld cache
            return this->dyldCache.uuidOfFileMatchesDyldCache(process, syscall, path);
        }
    }
#endif // TARGET_OS_OSX
    return false;
}

bool ProcessConfig::fileExists(const char* path, FileID* fileID, bool* notAFile) const
{
#if TARGET_OS_OSX
    // On macOS there are three dylibs under libSystem that exist for the simulator to use,
    // but we do not consider them "roots", so fileExists() returns false for them
    if ( simulatorFileMatchesDyldCache(path) )
        return false;
#endif // TARGET_OS_OSX
    return syscall.fileExists(path, fileID, notAFile);
}


const char* ProcessConfig::canonicalDylibPathInCache(const char* dylibPath) const
{
    if ( this->dyldCache.addr == nullptr )
        return nullptr;

    if ( const char* result = this->dyldCache.addr->getCanonicalPath(dylibPath) )
        return result;

#if TARGET_OS_OSX
    // on macOS support "Foo.framework/Foo" symlink
    char resolvedPath[PATH_MAX];
    if ( this->syscall.realpath(dylibPath, resolvedPath) ) {
        return this->dyldCache.addr->getCanonicalPath(resolvedPath);
    }
#endif
    return nullptr;
}


//
// MARK: --- global functions ---
//

#if BUILDING_DYLD
static char error_string[1024]; // FIXME: check if anything still needs the error_string global symbol, or if abort_with_payload superceeds it

void halt(const char* message)
{
    strlcpy(error_string, message, sizeof(error_string));
    CRSetCrashLogMessage(error_string);
    console("%s\n", message);
    /*
    if ( sSharedCacheLoadInfo.errorMessage != nullptr ) {
        // <rdar://problem/45957449> if dyld fails with a missing dylib and there is no shared cache, display the shared cache load error message
        log2("dyld cache load error: %s\n", sSharedCacheLoadInfo.errorMessage);
        log2("%s\n", message);
        strlcpy(error_string, "dyld cache load error: ", sizeof(error_string));
        strlcat(error_string, sSharedCacheLoadInfo.errorMessage, sizeof(error_string));
        strlcat(error_string, "\n", sizeof(error_string));
        strlcat(error_string, message, sizeof(error_string));
    }
    else {
        log2("%s\n", message);
        strlcpy(error_string, message, sizeof(error_string));
    }
*/
    // don't show back trace, during launch if symbol or dylib missing.  All info is in the error message
    if ( (gProcessInfo->errorKind == DYLD_EXIT_REASON_SYMBOL_MISSING) ||  (gProcessInfo->errorKind == DYLD_EXIT_REASON_DYLIB_MISSING) )
        gProcessInfo->terminationFlags = 1;

    gProcessInfo->errorMessage = error_string;
    char                payloadBuffer[EXIT_REASON_PAYLOAD_MAX_LEN];
    dyld_abort_payload* payload    = (dyld_abort_payload*)payloadBuffer;
    payload->version               = 1;
    payload->flags                 = (uint32_t)gProcessInfo->terminationFlags;
    payload->targetDylibPathOffset = 0;
    payload->clientPathOffset      = 0;
    payload->symbolOffset          = 0;
    int payloadSize                = sizeof(dyld_abort_payload);

    if ( gProcessInfo->errorTargetDylibPath != NULL ) {
        payload->targetDylibPathOffset = payloadSize;
        payloadSize += strlcpy(&payloadBuffer[payloadSize], gProcessInfo->errorTargetDylibPath, sizeof(payloadBuffer) - payloadSize) + 1;
    }
    if ( gProcessInfo->errorClientOfDylibPath != NULL ) {
        payload->clientPathOffset = payloadSize;
        payloadSize += strlcpy(&payloadBuffer[payloadSize], gProcessInfo->errorClientOfDylibPath, sizeof(payloadBuffer) - payloadSize) + 1;
    }
    if ( gProcessInfo->errorSymbol != NULL ) {
        payload->symbolOffset = payloadSize;
        payloadSize += strlcpy(&payloadBuffer[payloadSize], gProcessInfo->errorSymbol, sizeof(payloadBuffer) - payloadSize) + 1;
    }
    char truncMessage[EXIT_REASON_USER_DESC_MAX_LEN];
    strlcpy(truncMessage, message, EXIT_REASON_USER_DESC_MAX_LEN);
    const bool verbose = false;
    if ( verbose ) {
        console("dyld_abort_payload.version               = 0x%08X\n", payload->version);
        console("dyld_abort_payload.flags                 = 0x%08X\n", payload->flags);
        console("dyld_abort_payload.targetDylibPathOffset = 0x%08X (%s)\n", payload->targetDylibPathOffset, payload->targetDylibPathOffset ? &payloadBuffer[payload->targetDylibPathOffset] : "");
        console("dyld_abort_payload.clientPathOffset      = 0x%08X (%s)\n", payload->clientPathOffset, payload->clientPathOffset ? &payloadBuffer[payload->clientPathOffset] : "");
        console("dyld_abort_payload.symbolOffset          = 0x%08X (%s)\n", payload->symbolOffset, payload->symbolOffset ? &payloadBuffer[payload->symbolOffset] : "");
    }
    abort_with_payload(OS_REASON_DYLD, gProcessInfo->errorKind ? gProcessInfo->errorKind : DYLD_EXIT_REASON_OTHER, payloadBuffer, payloadSize, truncMessage, 0);
}
#endif // BUILDING_DYLD

void console(const char* format, ...)
{
    ::_simple_dprintf(2, "dyld[%d]: ", getpid());
    va_list list;
    va_start(list, format);
    ::_simple_vdprintf(2, format, list);
    va_end(list);
}




} // namespace dyld4
