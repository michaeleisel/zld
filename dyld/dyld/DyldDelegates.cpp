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
#include <string.h>
#include <mach-o/dyld_priv.h>
#if BUILDING_DYLD || BUILDING_CLOSURE_UTIL || BUILDING_SHARED_CACHE_UTIL
    #include <sys/socket.h>
    #include <sys/syslog.h>
    #include <sys/uio.h>
    #include <sys/un.h>
    #if __arm64__ || __arm__
        #include <System/sys/mman.h>
    #else
        #include <sys/mman.h>
    #endif
    #include <sys/attr.h>
    #include <sys/fsgetpath.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <sys/xattr.h>
    #include <sys/fcntl.h>
    #include <sys/sysctl.h>
    #include <dirent.h>
    #include <System/sys/csr.h>
    #include <System/sys/reason.h>
    #include <kern/kcdata.h>
    #include <System/machine/cpu_capabilities.h>
    #include <System/sys/content_protection.h>
    #include <sandbox/private.h>
    #include <sys/syscall.h>
    #include <sys/attr.h>
    #include <sys/vnode.h>
    #if !TARGET_OS_DRIVERKIT
        #include <vproc_priv.h>
    #endif
    // no libc header for send() syscall interface
    extern "C" ssize_t __sendto(int, const void*, size_t, int, const struct sockaddr*, socklen_t);
    #include "DyldProcessConfig.h"
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
#include "StringUtils.h"
#include "FileUtils.h"
#include "DyldDelegates.h"
#include "DebuggerSupport.h"
#include "Tracing.h"


using dyld3::MachOFile;
using dyld3::MachOAnalyzer;
using dyld3::FatFile;

namespace dyld4 {

uint64_t SyscallDelegate::amfiFlags(bool restricted, bool fairPlayEncryted) const
{
#if BUILDING_DYLD
    uint64_t amfiInputFlags  = 0;
    uint64_t amfiOutputFlags = 0;

    #if TARGET_OS_SIMULATOR
    amfiInputFlags |= AMFI_DYLD_INPUT_PROC_IN_SIMULATOR;
    #else
    if ( restricted )
        amfiInputFlags |= AMFI_DYLD_INPUT_PROC_HAS_RESTRICT_SEG;
    if ( fairPlayEncryted )
        amfiInputFlags |= AMFI_DYLD_INPUT_PROC_IS_ENCRYPTED;
    #endif

    if ( amfi_check_dyld_policy_self(amfiInputFlags, &amfiOutputFlags) != 0 ) {
        amfiOutputFlags = 0;
    }
    return amfiOutputFlags;
#else
    return _amfiFlags;
#endif
}

bool SyscallDelegate::internalInstall() const
{
#if TARGET_OS_SIMULATOR
    return false;
#elif BUILDING_DYLD && TARGET_OS_IPHONE
    uint32_t devFlags = *((uint32_t*)_COMM_PAGE_DEV_FIRM);
    return ((devFlags & 1) == 1);
#elif BUILDING_DYLD && TARGET_OS_OSX
    return (::csr_check(CSR_ALLOW_APPLE_INTERNAL) == 0);
#else
    return _internalInstall;
#endif
}

bool SyscallDelegate::isTranslated() const
{
#if BUILDING_DYLD && SUPPORT_ROSETTA
    return ((*(uint64_t*)_COMM_PAGE_CPU_CAPABILITIES64) & kIsTranslated);
#else
    return false;
#endif
}

bool SyscallDelegate::getCWD(char path[MAXPATHLEN]) const
{
#if BUILDING_DYLD
    // note: don't use getcwd() because it calls malloc()
    int fd = ::open(".", O_RDONLY|O_DIRECTORY, 0);
    if ( fd != -1 ) {
        int result = ::fcntl(fd, F_GETPATH, path);
        ::close(fd);
        return (result != -1);
    }
    return false;
#else
    if ( _cwd == nullptr )
        return false;
    ::strlcpy(path, _cwd, MAXPATHLEN);
    return true;
#endif
}

const GradedArchs& SyscallDelegate::getGradedArchs(const char* archName, bool keysOff) const
{
#if BUILDING_DYLD
    return dyld3::GradedArchs::forCurrentOS(keysOff, false); // FIXME, for AS macOS, need to know if main exe is OS binary
#elif BUILDING_CACHE_BUILDER
    return *_gradedArchs;
#else
    return dyld3::GradedArchs::forName(archName, keysOff);
#endif
}

int SyscallDelegate::openLogFile(const char* path) const
{
#if BUILDING_DYLD
    return ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
#else
    return -1;
#endif
}

bool SyscallDelegate::onHaswell() const
{
#if BUILDING_DYLD && TARGET_OS_OSX && __x86_64__
    struct host_basic_info info;
    mach_msg_type_number_t count    = HOST_BASIC_INFO_COUNT;
    mach_port_t            hostPort = mach_host_self();
    kern_return_t          result   = host_info(hostPort, HOST_BASIC_INFO, (host_info_t)&info, &count);
    if ( result == KERN_SUCCESS ) {
        if ( info.cpu_subtype == CPU_SUBTYPE_X86_64_H )
            return true;
    }
#endif
    return false;
}

bool SyscallDelegate::dtraceUserProbesEnabled() const
{
#if BUILDING_DYLD && !TARGET_OS_SIMULATOR
    uint8_t dofEnabled = *((uint8_t*)_COMM_PAGE_DTRACE_DOF_ENABLED);
    return ( (dofEnabled & 1) );
#else
    return false;
#endif
}

void SyscallDelegate::dtraceRegisterUserProbes(dof_ioctl_data_t* probes) const
{
#if BUILDING_DYLD
    int fd = ::open("/dev/" DTRACEMNR_HELPER, O_RDWR);
    if ( fd != -1 ) {
        // the probes data is variable length. The way this is handled is we just pass the pointer
        // to ioctlData and the kernel reads the full data from that.
        user_addr_t val = (user_addr_t)(unsigned long)probes;
        ::ioctl(fd, DTRACEHIOC_ADDDOF, &val);
        ::close(fd);
    }
#endif
}

void SyscallDelegate::dtraceUnregisterUserProbe(int registeredID) const
{
#if BUILDING_DYLD
    int fd = ::open("/dev/" DTRACEMNR_HELPER, O_RDWR, 0);
    if ( fd != -1 ) {
        ::ioctl(fd, DTRACEHIOC_REMOVE, registeredID);
        ::close(fd);
    }
#endif
}

void SyscallDelegate::getDyldCache(const dyld3::SharedCacheOptions& opts, dyld3::SharedCacheLoadInfo& loadInfo) const
{
#if BUILDING_DYLD
    dyld3::SharedCacheOptions localOpts = opts;
    localOpts.useHaswell                = onHaswell();
    loadDyldCache(localOpts, &loadInfo);
    if ( loadInfo.loadAddress != nullptr ) {
        gProcessInfo->processDetachedFromSharedRegion = opts.forcePrivate;
        gProcessInfo->sharedCacheFSID                 = loadInfo.FSID;
        gProcessInfo->sharedCacheFSObjID              = loadInfo.FSObjID;
        gProcessInfo->sharedCacheSlide                = loadInfo.slide;
        gProcessInfo->sharedCacheBaseAddress          = (unsigned long)loadInfo.loadAddress;
        loadInfo.loadAddress->getUUID(gProcessInfo->sharedCacheUUID);
        dyld3::kdebug_trace_dyld_cache(loadInfo.path, gProcessInfo);
    }
#else
    if ( _dyldCache != nullptr ) {
        loadInfo.loadAddress = _dyldCache;
        strcpy(loadInfo.path, "supplied");
        loadInfo.slide       = _dyldCache->slide();
    }
    else {
        loadInfo.loadAddress = nullptr;
        loadInfo.slide       = 0;
        // if a cache not already set, use current one
//        size_t len;
//        loadInfo.loadAddress = (DyldSharedCache*)_dyld_get_shared_cache_range(&len);
//        if ( const char* path = dyld_shared_cache_file_path() )
//            strcpy(loadInfo.path, path);
    }
    loadInfo.errorMessage = nullptr;
#endif
}

// walk directory and return all dirs/files therein
void SyscallDelegate::forEachInDirectory(const char* dirPath, bool dirsOnly, void (^handler)(const char* pathInDir)) const
{
#if BUILDING_DYLD
    // NOTE: opendir() uses malloc(), so we just lower level getattrlistbulk() instead
    int fd = ::open(dirPath, O_RDONLY|O_DIRECTORY, 0);
    if ( fd != -1 ) {
        struct attrlist attrList;
        bzero(&attrList, sizeof(attrList));
        attrList.bitmapcount = ATTR_BIT_MAP_COUNT;
        attrList.commonattr  = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_OBJTYPE | ATTR_CMN_NAME;
        bool more = true;
        while ( more ) {
            uint8_t attrBuf[512];
            int retcount = ::getattrlistbulk(fd, &attrList, &attrBuf[0], sizeof(attrBuf), 0);
            if ( retcount <= 0 ) {
                more = false;
            }
            else {
                struct attr_layout {
                    uint32_t        length;
                    attribute_set_t returned;
                    attrreference_t name_info;
                    fsobj_type_t    type;
                };
                const attr_layout* entry = (attr_layout*)&attrBuf[0];
                for (int index=0; index < retcount; ++index) {
                    const char* entryName = (char*)(&entry->name_info) + entry->name_info.attr_dataoffset;
                    bool use = false;
                    if ( entry->returned.commonattr & ATTR_CMN_OBJTYPE ) {
                        if ( entry->type == VDIR ) {
                            if ( dirsOnly )
                                use = true;
                        }
                        else if ( entry->type == VREG ) {
                            if ( !dirsOnly )
                                use = true;
                        }
                    }
                    if ( use ) {
                        char newPath[PATH_MAX];
                        ::strlcpy(newPath, dirPath, PATH_MAX);
                        ::strlcat(newPath, "/", PATH_MAX);
                        ::strlcat(newPath, entryName, PATH_MAX);
                        handler(newPath);
                    }
                    entry = (attr_layout*)((uint8_t*)entry + entry->length);
                }
            }
        }
        ::close(fd);
    }
#else
    const auto& pos = _dirMap.find(dirPath);
    if ( pos != _dirMap.end() ) {
        for (const char* node : pos->second) {
            char newPath[PATH_MAX];
            ::strlcpy(newPath, dirPath, PATH_MAX);
            ::strlcat(newPath, "/", PATH_MAX);
            ::strlcat(newPath, node, PATH_MAX);
            handler(newPath);
        }
    }
#endif
}

bool SyscallDelegate::getDylibInfo(const char* dylibPath, dyld3::Platform platform, const GradedArchs& archs, uint32_t& version, char installName[PATH_MAX]) const
{
#if BUILDING_DYLD
    __block Diagnostics diag;
    __block bool        result = false;
    this->withReadOnlyMappedFile(diag, dylibPath, false, ^(const void* mapping, size_t mappedSize, bool isOSBinary, const FileID&, const char*) {
        bool             missingSlice;
        uint64_t         fileOffset = 0;
        uint64_t         fileLength = mappedSize;
        const FatFile*   ff         = (FatFile*)mapping;
        const MachOFile* mf         = nullptr;
        if ( ff->isFatFileWithSlice(diag, mappedSize, archs, true, fileOffset, fileLength, missingSlice) ) {
            mf = (MachOAnalyzer*)((uint8_t*)mapping + fileOffset);
        }
        else if ( ((MachOFile*)mapping)->isMachO(diag, fileLength) ) {
            mf = (MachOAnalyzer*)mapping;
        }
        else {
            return;
        }
        if ( mf->isDylib() && mf->loadableIntoProcess(platform, dylibPath) ) {
            const char* dylibInstallName;
            uint32_t    compatVersion;
            uint32_t    currentVersion;
            if ( mf->getDylibInstallName(&dylibInstallName, &compatVersion, &currentVersion) ) {
                version = currentVersion;
                ::strlcpy(installName, dylibInstallName, PATH_MAX);
                result = true;
            }
        }
    });
    return result;
#else
    const auto& pos = _dylibInfoMap.find(dylibPath);
    if ( pos != _dylibInfoMap.end() ) {
        version = pos->second.version;
        ::strlcpy(installName, pos->second.installName, PATH_MAX);
        return true;
    }
    return false;
#endif
}

// precondition: to be secure homeDir must be a realpath
bool SyscallDelegate::isContainerized(const char* homeDir) const
{
    // FIXME: rdar://79896751 (OS should provide a way to return if app is containerized)
    return homeDir && (strncmp(homeDir, "/private/var/mobile/Containers/Data/", 36) == 0);
}

bool SyscallDelegate::isMaybeContainerized(const char* homeDir) const
{
    // FIXME: rdar://79896751 (OS should provide a way to return if app is containerized)
    return homeDir && (strstr(homeDir, "/var/mobile/Containers/Data/") != 0);
}

bool SyscallDelegate::fileExists(const char* path, FileID* fileID, bool* notAFile) const
{
#if BUILDING_DYLD || BUILDING_CLOSURE_UTIL
    struct stat sb;
    bool found = (this->stat(path, &sb) == 0);
    if ( found ) {
        bool isFile = S_ISREG(sb.st_mode);
        if ( notAFile != nullptr )
            *notAFile = !isFile;
        if ( !isFile )
            found = false;
    }
    if ( found && (fileID != nullptr) ) {
        uint64_t inode = 0;
#if __LP64__
        inode = sb.st_ino;
#else
        inode = sb.st_ino & 0xFFFFFFFF;  // HACK, work around inode randomly getting high bit set, making them uncomparable.
#endif
        uint64_t mtime = sb.st_mtime;
        *fileID = FileID(inode, mtime, true);
    }
    return found;
#elif BUILDING_CACHE_BUILDER
    if ( path[0] != '/' )
        return false;
    bool found = (_mappedOtherDylibs.count(path) != 0);
    if ( !found  ) {
        std::string betterPath = normalize_absolute_file_path(path);
        found = (_mappedOtherDylibs.count(betterPath) != 0);
    }
    if ( found ) {
        if ( notAFile != nullptr )
            *notAFile = false;
        if ( fileID != nullptr )
            *fileID = FileID::none();
    }
    return found;
#else
    return false; // FIXME
#endif
}

bool SyscallDelegate::dirExists(const char* path) const
{
    struct stat sb;
    bool found = (this->stat(path, &sb) == 0);
    if ( found ) {
        bool isDir = S_ISDIR(sb.st_mode);
        if ( !isDir )
            found = false;
    }
    return found;
}

bool SyscallDelegate::mkdirs(const char* path) const
{
#if BUILDING_DYLD && !TARGET_OS_SIMULATOR
    char dirs[::strlen(path)+1];
    ::strcpy(dirs, path);
    char* lastSlash = ::strrchr(dirs, '/');
    if ( lastSlash == NULL )
        return false;
    lastSlash[1] = '\0';
    struct stat stat_buf;
    if ( this->stat(dirs, &stat_buf) != 0 ) {
        char* afterSlash = &dirs[1];
        char* slash;
        while ( (slash = strchr(afterSlash, '/')) != NULL ) {
            *slash = '\0';
            ::mkdir(dirs, S_IRWXU | S_IRGRP|S_IXGRP | S_IROTH|S_IXOTH);
            //printf("mkdir(%s)\n", dirs);
            *slash = '/';
            afterSlash = slash+1;
        }
    }
    return true;
#else
    return false; // FIXME
#endif
}

bool SyscallDelegate::realpath(const char* input, char output[1024]) const
{
#if BUILDING_DYLD
    int fd = dyld3::open(input, O_RDONLY, 0);
    if ( fd != -1 ) {
        // path is actual file, use F_GETPATH
        bool success = (fcntl(fd, F_GETPATH, output) == 0);
        ::close(fd);
        return success;
    }
    // no such file, try realpath()ing the directory part, then add back leaf part
    char dir[PATH_MAX];
    strlcpy(dir, input, PATH_MAX);
    char* lastSlash = ::strrchr(dir, '/');
    const char* leaf = nullptr;
    if ( lastSlash != nullptr ) {
        *lastSlash = '\0';
        leaf = &input[lastSlash-dir+1];
    }
    else {
        strcpy(dir, ".");
        leaf = input;
    }
    fd = dyld3::open(dir, O_RDONLY|O_DIRECTORY, 0);
    if ( fd == -1 )
        return false;
    // use F_GETPATH to get real dir path
    bool success = (fcntl(fd, F_GETPATH, output) == 0);
    ::close(fd);
    if ( success ) {
        strlcat(output, "/", PATH_MAX);
        strlcat(output, leaf, PATH_MAX);
        return true;
    }
    return false;
#else
    return false; // FIXME
#endif
}

const void* SyscallDelegate::mapFileReadOnly(Diagnostics& diag, const char* path, size_t* size, FileID* fileID, bool* isOSBinary, char* realerPath) const
{
#if BUILDING_DYLD || BUILDING_CLOSURE_UTIL
    struct stat statbuf;
    if ( this->stat(path, &statbuf) == -1 ) {
        int err = errno;
        if ( (err == EPERM) && this->sandboxBlockedStat(path) )
            diag.error("file system sandbox blocked stat()");
        else if ( err == ENOENT )
            diag.error("no such file");
        else
            diag.error("stat() failed with errno=%d", errno);
        return nullptr;
    }

    // check for tombstone file
    if ( statbuf.st_size == 0 )
        return nullptr;

    int fd = this->open(path, O_RDONLY, 0);
    if ( fd < 0 ) {
        int err = errno;
        if ( (err == EPERM) && this->sandboxBlockedOpen(path) )
            diag.error("file system sandbox blocked open()");
        else
            diag.error("open() failed with errno=%d", err);
        return nullptr;
    }

    const void* result = ::mmap(nullptr, (size_t)statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if ( result == MAP_FAILED ) {
        diag.error("mmap(size=0x%0lX) failed with errno=%d", (size_t)statbuf.st_size, errno);
        ::close(fd);
        return nullptr;
    }

    // set optional return values
    if ( size != nullptr )
        *size = (size_t)statbuf.st_size;
    if ( fileID != nullptr ) {
        uint64_t inode = 0;
#if __LP64__
        inode = statbuf.st_ino;
#else
        inode = statbuf.st_ino & 0xFFFFFFFF;
#endif
        uint64_t mtime = statbuf.st_mtime;
        *fileID = FileID(inode, mtime, true);
    }
    if ( realerPath != nullptr ) {
        this->getpath(fd, realerPath);
    }
    if ( isOSBinary != nullptr ) {
        // if this is an arm64e mach-o or a fat file with an arm64e slice we need to record if it is an OS binary
        *isOSBinary = false;
        const MachOAnalyzer* ma = (MachOAnalyzer*)result;
        if ( ma->hasMachOMagic() ) {
            if ( (ma->cputype == CPU_TYPE_ARM64) && ((ma->cpusubtype & ~CPU_SUBTYPE_MASK) == CPU_SUBTYPE_ARM64E) ) {
                if ( ma->isOSBinary(fd, 0, statbuf.st_size) )
                    *isOSBinary = true;
            }
        }
        else if ( const FatFile* fat = FatFile::isFatFile(result) ) {
            fat->forEachSlice(diag, statbuf.st_size, ^(uint32_t sliceCpuType, uint32_t sliceCpuSubType, const void* sliceStart, uint64_t sliceSize, bool& stop) {
                if ( (sliceCpuType == CPU_TYPE_ARM64) && ((sliceCpuSubType & ~CPU_SUBTYPE_MASK) == CPU_SUBTYPE_ARM64E) ) {
                    uint64_t sliceOffset = (uint8_t*)sliceStart-(uint8_t*)result;
                    const MachOAnalyzer* sliceMA = (MachOAnalyzer*)sliceStart;
                    if ( sliceMA->isOSBinary(fd, sliceOffset, sliceSize) )
                        *isOSBinary = true;
                }
            });
        }

    }

    ::close(fd);
    return result;
#else
    return nullptr; // FIXME
#endif
}

void SyscallDelegate::unmapFile(const void* buffer, size_t size) const
{
#if BUILDING_DYLD
    ::munmap((void*)buffer, size);
#else
    // FIXME
#endif
}

void SyscallDelegate::withReadOnlyMappedFile(Diagnostics& diag, const char* path, bool checkIfOSBinary,
                                             void (^handler)(const void* mapping, size_t mappedSize, bool isOSBinary, const FileID& fileID, const char* realPath)) const
{
#if BUILDING_DYLD || BUILDING_CLOSURE_UTIL
    size_t   mappedSize;
    FileID   fileID = FileID::none();
    bool     isOSBinary = false;
    char     realerPath[PATH_MAX];
    if ( const void* mapping = this->mapFileReadOnly(diag, path, &mappedSize, &fileID, (checkIfOSBinary ? &isOSBinary : nullptr), realerPath) ) {
        handler(mapping, mappedSize, isOSBinary, fileID, realerPath);
        this->unmapFile(mapping, mappedSize);
    }
#elif BUILDING_CACHE_BUILDER
    const auto& pos = _mappedOtherDylibs.find(path);
    if ( pos == _mappedOtherDylibs.end() ) {
        std::string betterPath = normalize_absolute_file_path(path);
        const auto& pos2 = _mappedOtherDylibs.find(betterPath);
        if ( pos2 != _mappedOtherDylibs.end() ) {
            handler(pos2->second.mappingStart, pos2->second.mappingSize, true, FileID::none(), path);
        }
    }
    else {
        handler(pos->second.mappingStart, pos->second.mappingSize, true, FileID::none(), path);
    }
#else

#endif
}

bool SyscallDelegate::getFileAttribute(const char* path, const char* attrName, Array<uint8_t>& attributeBytes) const
{
#if BUILDING_DYLD
    ssize_t attrSize = ::getxattr(path, attrName, attributeBytes.begin(), attributeBytes.maxCount(), 0, 0);
    if ( attrSize == -1 )
        return false;
    attributeBytes.resize(attrSize);
    return true;
#else
    return false; // FIXME
#endif
}

bool SyscallDelegate::setFileAttribute(const char* path, const char* attrName, const Array<uint8_t>& attributeBytes) const
{
#if BUILDING_DYLD
    int result = ::chmod(path, S_IRUSR | S_IWUSR); // file has to be writable to alter attributes
    if ( result != 0 )
        return false;
    // try replace first, then fallback to adding attribute
    result = ::setxattr(path, attrName, attributeBytes.begin(), attributeBytes.count(), 0, XATTR_REPLACE);
    if ( result != 0 )
        result = ::setxattr(path, attrName, attributeBytes.begin(), attributeBytes.count(), 0, 0);
    int result2 = ::chmod(path, S_IRUSR); // switch back file permissions
    return (result == 0) && (result2 == 0);
#else
    // FIXME
    return false;
#endif
}


bool SyscallDelegate::saveFileWithAttribute(Diagnostics& diag, const char* path, const void* buffer, size_t size, const char* attrName, const Array<uint8_t>& attributeBytes) const
{
#if BUILDING_DYLD
    // write to a temp file
    char tempPath[PATH_MAX];
    ::strlcpy(tempPath, path, PATH_MAX);
    int   mypid = getpid();
    char  pidBuf[16];
    char* s = pidBuf;
    *s++    = '.';
    putHexByte((mypid >> 24) & 0xFF, s);
    putHexByte((mypid >> 16) & 0xFF, s);
    putHexByte((mypid >> 8) & 0xFF, s);
    putHexByte(mypid & 0xFF, s);
    *s = '\0';
    ::strlcat(tempPath, pidBuf, PATH_MAX);
#if TARGET_OS_OSX
    int fd = dyld3::open(tempPath, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
#else
    int fd = ::open_dprotected_np(tempPath, O_WRONLY | O_CREAT, PROTECTION_CLASS_D, 0, S_IRUSR | S_IWUSR);
#endif
    if ( fd == -1 ) {
        diag.error("open/open_dprotected_np(%s) failed, errno=%d", tempPath, errno);
        return false;
    }
    int result = ::ftruncate(fd, size);
    if ( result == -1 ) {
        diag.error("ftruncate(%lu) failed, errno=%d", size, errno);
        return false;
    }
    ssize_t wroteSize = ::write(fd, buffer, size);
    if ( wroteSize != size ) {
        diag.error("write() failed, errno=%d", errno);
        return false;
    }
    result = ::fsetxattr(fd, attrName, attributeBytes.begin(), attributeBytes.count(), 0, 0);
    if ( result == -1 ) {
        diag.error("fsetxattr(%s) failed, errno=%d", attrName, errno);
        return false;
    }
    result = ::fchmod(fd, S_IRUSR);
    if ( result == -1 ) {
        diag.error("fchmod(S_IRUSR) failed, errno=%d", errno);
        return false;
    }
    result = ::close(fd);
    if ( result == -1 ) {
        diag.error("close() failed, errno=%d", errno);
        return false;
    }
    result = ::rename(tempPath, path);
    if ( result == -1 ) {
        diag.error("rename(%s, %s) failed, errno=%d", tempPath, path, errno);
        return false;
    }
    return true;
#else
    return false; // FIXME
#endif
}

void SyscallDelegate::getpath(int fd, char realerPath[]) const
{
#if BUILDING_DYLD
    ::fcntl(fd, F_GETPATH, realerPath);
#elif BUILDING_CLOSURE_UTIL || BUILDING_SHARED_CACHE_UTIL
    if ( _overlayPath != nullptr ) {
        char tempPath[MAXPATHLEN];
        bool success = (fcntl(fd, F_GETPATH, tempPath) == 0);
        if ( success && (strncmp(tempPath, _overlayPath, strlen(_overlayPath)) == 0) ) {
            // Overlay was used, remove it
            strcpy(realerPath, &tempPath[strlen(_overlayPath)]);
            return;
        }
        // Fall though to other cases as this was only an overlay
    }
    if ( _rootPath != nullptr ) {
        char tempPath[MAXPATHLEN];
        bool success = (fcntl(fd, F_GETPATH, tempPath) == 0);
        if ( success ) {
            // Assuming was used, remove it
            if ( strncmp(tempPath, _rootPath, strlen(_rootPath)) == 0 )
                strcpy(realerPath, &tempPath[strlen(_rootPath)]);
            else
                strcpy(realerPath, tempPath);
        }
    } else {
        ::fcntl(fd, F_GETPATH, realerPath);
    }
#else
    // FIXME
    abort();
#endif
}

int SyscallDelegate::getpid() const
{
#if BUILDING_DYLD
    return ::getpid();
#else
    // FIXME
    return 100;
#endif
}

bool SyscallDelegate::sandboxBlocked(const char* path, const char* kind) const
{
#if BUILDING_DYLD && !TARGET_OS_SIMULATOR && !TARGET_OS_DRIVERKIT
    sandbox_filter_type filter = (sandbox_filter_type)(SANDBOX_FILTER_PATH | SANDBOX_CHECK_NO_REPORT);
    return ( sandbox_check(this->getpid(), kind, filter, path) > 0 );
#else
    return false;
#endif
}

bool SyscallDelegate::sandboxBlockedMmap(const char* path) const
{
    return sandboxBlocked(path, "file-map-executable");
}

bool SyscallDelegate::sandboxBlockedOpen(const char* path) const
{
    return sandboxBlocked(path, "file-read-data");
}

bool SyscallDelegate::sandboxBlockedStat(const char* path) const
{
    return sandboxBlocked(path, "file-read-metadata");
}


SyscallDelegate::DyldCommPage::DyldCommPage()
{
    bzero(this, sizeof(DyldCommPage));
}

SyscallDelegate::DyldCommPage SyscallDelegate::dyldCommPageFlags() const
{
#if BUILDING_DYLD
    return *((DyldCommPage*)_COMM_PAGE_DYLD_FLAGS);
#else
    return _commPageFlags;
#endif
}

void SyscallDelegate::setDyldCommPageFlags(DyldCommPage value) const
{
#if BUILDING_DYLD && !TARGET_OS_SIMULATOR
    ::sysctlbyname("kern.dyld_flags", nullptr, 0, &value, sizeof(value));
#endif
#if !BUILDING_DYLD
    *((uint64_t*)&_commPageFlags) = *((uint64_t*)&value);
#endif
}

bool SyscallDelegate::bootVolumeWritable() const
{
#if BUILDING_DYLD
    struct statfs statBuffer;
    if ( statfs("/", &statBuffer) == 0 ) {
        if ( strcmp(statBuffer.f_fstypename, "apfs") == 0 ) {
            if ( (statBuffer.f_flags & (MNT_RDONLY | MNT_SNAPSHOT)) == (MNT_RDONLY | MNT_SNAPSHOT) ) {
                return false;
            }
        }
    }
    return true;
#else
    return false; // FIXME
#endif
}

int SyscallDelegate::open(const char* path, int flags, int other) const
{
#if BUILDING_DYLD
    return dyld3::open(path, flags, other);
#elif BUILDING_CLOSURE_UTIL || BUILDING_SHARED_CACHE_UTIL
    if ( _overlayPath != nullptr ) {
        char altPath[PATH_MAX];
        strlcpy(altPath, _overlayPath, PATH_MAX);
        if ( path[0] != '/' )
            strlcat(altPath, "/", PATH_MAX);
        strlcat(altPath, path, PATH_MAX);
        int result = dyld3::open(altPath, flags, other);
        if ( result >= 0 )
            return result;

        // Fall though to other cases as this was only an overlay
    }
    if ( _rootPath != nullptr ) {
        char altPath[PATH_MAX];
        strlcpy(altPath, _rootPath, PATH_MAX);
        if ( path[0] != '/' )
            strlcat(altPath, "/", PATH_MAX);
        strlcat(altPath, path, PATH_MAX);
        return dyld3::open(altPath, flags, other);
    } else {
        return dyld3::open(path, flags, other);
    }
#else
    // FIXME
    abort();
#endif
}

int SyscallDelegate::close(int fd) const
{
#if BUILDING_DYLD || BUILDING_CLOSURE_UTIL
    return ::close(fd);
#else
    abort();
    // FIXME
#endif
}

ssize_t SyscallDelegate::pread(int fd, void* buffer, size_t len, size_t offset) const
{
#if BUILDING_DYLD
    return ::pread(fd, buffer, len, offset);
#else
    abort();
    // FIXME
#endif
}

ssize_t SyscallDelegate::pwrite(int fd, const void* buffer, size_t len, size_t offset) const
{
#if BUILDING_DYLD
    return ::pwrite(fd, buffer, len, offset);
#else
    abort();
    // FIXME
#endif
}

int SyscallDelegate::mprotect(void* start, size_t size, int prot) const
{
#if BUILDING_DYLD
    return ::mprotect(start, size, prot);
#else
    abort();
    // FIXME
#endif
}


int SyscallDelegate::unlink(const char* path) const
{
#if BUILDING_DYLD
    return ::unlink(path);
#else
    abort();
    // FIXME
#endif
}

int SyscallDelegate::fcntl(int fd, int cmd, void* param) const
{
#if BUILDING_DYLD || BUILDING_CLOSURE_UTIL
    return ::fcntl(fd, cmd, param);
#else
    abort();
    // FIXME
#endif
}

int SyscallDelegate::fstat(int fd, struct stat* buf) const
{
#if BUILDING_DYLD || BUILDING_CACHE_BUILDER
    return ::fstat(fd, buf);
#else
    abort();
    // FIXME
#endif
}

int SyscallDelegate::stat(const char* path, struct stat* buf) const
{
#if BUILDING_DYLD
    return dyld3::stat(path, buf);
#elif BUILDING_CLOSURE_UTIL || BUILDING_SHARED_CACHE_UTIL
    if ( _overlayPath != nullptr ) {
        char altPath[PATH_MAX];
        strlcpy(altPath, _overlayPath, PATH_MAX);
        if ( path[0] != '/' )
            strlcat(altPath, "/", PATH_MAX);
        strlcat(altPath, path, PATH_MAX);
        int result = dyld3::stat(altPath, buf);
        if ( result == 0 )
            return result;

        // Fall though to other cases as this was only an overlay
    }
    if ( _rootPath != nullptr ) {
        char altPath[PATH_MAX];
        strlcpy(altPath, _rootPath, PATH_MAX);
        if ( path[0] != '/' )
            strlcat(altPath, "/", PATH_MAX);
        strlcat(altPath, path, PATH_MAX);
        return dyld3::stat(altPath, buf);
    } else {
        return dyld3::stat(path, buf);
    }
#else
    abort();
    // FIXME
#endif
}

void* SyscallDelegate::mmap(void* addr, size_t len, int prot, int flags, int fd, size_t offset) const
{
#if BUILDING_DYLD || BUILDING_CLOSURE_UTIL
    return ::mmap(addr, len, prot, flags, fd, offset);
#else
    abort();
    // FIXME
#endif
}

int SyscallDelegate::munmap(void* addr, size_t len) const
{
#if BUILDING_DYLD
    return ::munmap(addr, len);
#else
    abort();
    // FIXME
#endif
}

int SyscallDelegate::socket(int domain, int type, int protocol) const
{
#if BUILDING_DYLD
    return ::socket(domain, type, protocol);
#else
    abort();
    // FIXME
#endif
}

int SyscallDelegate::connect(int socket, const struct sockaddr* address, socklen_t address_len) const
{
#if BUILDING_DYLD
    return ::connect(socket, address, address_len);
#else
    abort();
    // FIXME
#endif
}

kern_return_t SyscallDelegate::vm_protect(task_port_t task, vm_address_t addr, vm_size_t size, bool which, uint32_t perms) const
{
#if BUILDING_DYLD
    return ::vm_protect(task, addr, size, which, perms);
#else
    abort();
    // FIXME
#endif
}

int SyscallDelegate::mremap_encrypted(void* p, size_t len, uint32_t id, uint32_t cpuType, uint32_t cpuSubtype) const
{
#if BUILDING_DYLD && !TARGET_OS_SIMULATOR && (__arm64__ || __arm__)
    return ::mremap_encrypted(p, len, id, cpuType, cpuSubtype);
#else
    abort();
    // FIXME
#endif
}

ssize_t SyscallDelegate::fsgetpath(char result[], size_t resultBufferSize, uint64_t fsID, uint64_t objID) const
{
#if BUILDING_DYLD
    fsid_t      fsid  = *reinterpret_cast<fsid_t*>(&fsID);
    return ::fsgetpath(result, resultBufferSize, &fsid, objID);
#else
    const auto& pos = _fileIDsToPath.find(makeFsIdPair(fsID,objID));
    if ( pos != _fileIDsToPath.end() ) {
        const std::string& str = pos->second;
        strlcpy(result, str.c_str(), resultBufferSize);
        return strlen(str.c_str());
    }
    return -1;
#endif
}


} // namespace
