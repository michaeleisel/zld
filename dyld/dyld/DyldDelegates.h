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


#ifndef DyldDelegates_h
#define DyldDelegates_h

#include <stdint.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/dtrace.h>

#if !BUILDING_DYLD
  #include <vector>
  #include <map>
#endif

#include "Defines.h"
#include "Array.h"
#include "MachOAnalyzer.h"
#include "SharedCacheRuntime.h"

class DyldSharedCache;

using dyld3::MachOAnalyzer;
using dyld3::GradedArchs;
using dyld3::Array;

namespace dyld4 {

// for detecting symlinks and hard links, so dyld does not load same file twice
struct FileID
{
    FileID() = delete;
    FileID(uint64_t inode, uint64_t mtime, bool isValid) : iNode(inode), modTime(mtime), isValid(isValid) { }

    bool            valid() const { return isValid; }
    uint64_t        inode() const { return iNode; }
    uint64_t        mtime() const { return modTime; }

    static FileID   none() { return FileID(0, 0, false); }

    bool            operator==(const FileID& other) const {
        // Note, if either this or other is invalid, the result is false
        return (isValid && other.isValid) && (iNode==other.iNode) && (modTime==other.modTime);
    }
    bool            operator!=(const FileID& other) const { return !(*this == other); }

private:
    uint64_t        iNode   = 0;
    uint64_t        modTime = 0;
    bool            isValid = false;

};

// all dyld syscalls goes through this delegate, which enables cache building and off line testing
class SyscallDelegate
{
public:
    uint64_t            amfiFlags(bool restricted, bool fairPlayEncryted) const;
    bool                internalInstall() const;
    bool                isTranslated() const;
    bool                getCWD(char path[MAXPATHLEN]) const;
    const GradedArchs&  getGradedArchs(const char* archName, bool keysOff) const;
    int                 openLogFile(const char* path) const;
    void                getDyldCache(const dyld3::SharedCacheOptions& opts, dyld3::SharedCacheLoadInfo& loadInfo) const;
    void                forEachInDirectory(const char* dir, bool dirs, void (^handler)(const char* pathInDir)) const;
    bool                getDylibInfo(const char* dylibPath, dyld3::Platform platform, const GradedArchs& archs, uint32_t& version, char installName[PATH_MAX]) const;
    bool                isContainerized(const char* homeDir) const;
    bool                isMaybeContainerized(const char* homeDir) const;
    bool                fileExists(const char* path, FileID* fileID=nullptr, bool* notAFile=nullptr) const;
    bool                dirExists(const char* path) const;
    bool                mkdirs(const char* path) const;
    bool                realpath(const char* input, char output[1024]) const;
    const void*         mapFileReadOnly(Diagnostics& diag, const char* path, size_t* size=nullptr, FileID* fileID=nullptr, bool* isOSBinary=nullptr, char* betterPath=nullptr) const;
    void                unmapFile(const void* buffer, size_t size) const;
    void                withReadOnlyMappedFile(Diagnostics& diag, const char* path, bool checkIfOSBinary, void (^handler)(const void* mapping, size_t mappedSize, bool isOSBinary, const FileID& fileID, const char* realPath)) const;
    bool                getFileAttribute(const char* path, const char* attrName, Array<uint8_t>& attributeBytes) const;
    bool                setFileAttribute(const char* path, const char* attrName, const Array<uint8_t>& attributeBytes) const;
    bool                saveFileWithAttribute(Diagnostics& diag, const char* path, const void* buffer, size_t size, const char* attrName, const Array<uint8_t>& attributeBytes) const;
    bool                sandboxBlockedMmap(const char* path) const;
    bool                sandboxBlockedOpen(const char* path) const;
    bool                sandboxBlockedStat(const char* path) const;
    bool                sandboxBlocked(const char* path, const char* kind) const;
    void                getpath(int fd, char* realerPath) const;
    bool                onHaswell() const;
    bool                dtraceUserProbesEnabled() const;
    void                dtraceRegisterUserProbes(dof_ioctl_data_t* probes) const;
    void                dtraceUnregisterUserProbe(int registeredID) const;

    struct DyldCommPage
    {
        uint64_t  forceCustomerCache  :  1,     // dyld_flags=0x00000001
                  testMode            :  1,     // dyld_flags=0x00000002
                  forceDevCache       :  1,     // dyld_flags=0x00000004
                  unusedFlagsLow      : 14,
                  enableCompactInfo   :  1,     // dyld_flags=0x00020000
                  forceRODataConst    :  1,     // dyld_flags=0x00040000
                  forceRWDataConst    :  1,     // dyld_flags=0x00080000
                  unusedFlagsHigh     : 12,
                  libPlatformRoot     :  1,
                  libPthreadRoot      :  1,
                  libKernelRoot       :  1,
                  bootVolumeWritable  :  1,
                  unused3             : 28;

                  DyldCommPage();
    };
    static_assert(sizeof(DyldCommPage) == sizeof(uint64_t));



    DyldCommPage        dyldCommPageFlags() const;
    void                setDyldCommPageFlags(DyldCommPage value) const;
    bool                bootVolumeWritable() const;

    // posix level
    int                 getpid() const;
    int                 open(const char* path, int flag, int other) const;
    int                 close(int) const;
    ssize_t             pread(int fd, void* bufer, size_t len, size_t offset) const;
    ssize_t             pwrite(int fd, const void* bufer, size_t len, size_t offset) const;
    int                 mprotect(void* start, size_t, int prot) const;
    int                 unlink(const char* path) const;
    int                 fcntl(int fd, int cmd, void*) const;
    int                 fstat(int fd, struct stat* buf) const;
    int                 stat(const char* path, struct stat* buf) const;
    void*               mmap(void* addr, size_t len, int prot, int flags, int fd, size_t offset) const;
    int                 munmap(void* addr, size_t len) const;
    int                 socket(int domain, int type, int protocol) const;
    int                 connect(int socket, const struct sockaddr* address, socklen_t address_len) const;
    kern_return_t       vm_protect(task_port_t, vm_address_t, vm_size_t, bool which, uint32_t perms) const;
    int                 mremap_encrypted(void*, size_t len, uint32_t, uint32_t cpuType, uint32_t cpuSubtype) const;
    ssize_t             fsgetpath(char result[], size_t resultBufferSize, uint64_t fsid, uint64_t objid) const;

#if !BUILDING_DYLD
    typedef std::map<std::string, std::vector<const char*>> PathToPathList;
    struct VersionAndInstallName { uint32_t version; const char* installName; };
    typedef std::map<std::string, VersionAndInstallName> PathToDylibInfo;
    typedef std::map<uint64_t, std::string> FileIDsToPath;

    static uint64_t     makeFsIdPair(uint64_t fsid, uint64_t objid) { return (fsid << 32) |  objid; }

#if BUILDING_CACHE_BUILDER
    struct MappingInfo { const void* mappingStart; size_t mappingSize; };
    typedef std::map<std::string, MappingInfo> PathToMapping;
#endif

    uint64_t                _amfiFlags       = -1;
    DyldCommPage            _commPageFlags;
    bool                    _internalInstall = false;
    const char*             _cwd             = nullptr;
    PathToPathList          _dirMap;
    const DyldSharedCache*  _dyldCache       = nullptr;
    PathToDylibInfo         _dylibInfoMap;
    FileIDsToPath           _fileIDsToPath;
#if BUILDING_CACHE_BUILDER
    PathToMapping           _mappedOtherDylibs;
    const GradedArchs*      _gradedArchs    = nullptr;
#endif

#if BUILDING_CLOSURE_UTIL || BUILDING_SHARED_CACHE_UTIL
    // An alternative root path to use.  This will not fall back to /
    // Note this must be a real path
    const char*             _rootPath       = nullptr;
    // An overlay to layer on top of the root path.  It must be a real path
    const char*             _overlayPath    = nullptr;
#endif

#endif // !BUILDING_DYLD
};


} // namespace



#endif /* DyldDelegates_h */
