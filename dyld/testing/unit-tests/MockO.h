/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2020 Apple Inc. All rights reserved.
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

#ifndef MockO_h
#define MockO_h

#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_priv.h>
#include <stdio.h>

#include <vector>
#include <string>
#include <optional>

#include "Defines.h"
#include "MachOAnalyzer.h"

using dyld3::FatFile;
using dyld3::MachOAnalyzer;
using dyld3::Platform;

//
// Utility to dynamically build a final linked mach-o file.
// All info is stored in ivars until header() is called.
//
class VIS_HIDDEN MockO
{
public:

    // Just calling the constructor is enough to make a valid (but simple) mach-o for the given arch/platform/version
                         MockO(uint32_t filetype, const char* arch, Platform plat=Platform::macOS, const char* minOS=nullptr, const char* sdk=nullptr);
                         ~MockO();

    // These methods are called to add more complex features to the basic mach-o file
    void                 customizeMakeZippered();
    void                 customizeInstallName(const char* path, uint32_t compatVers=1, uint32_t curVers=1);
    void                 customizeAddDependentDylib(const char* path, bool isWeak=false, bool isUpward=false, bool isReexport=false, uint32_t compatVers=1, uint32_t curVers=1);
    void                 customizeAddDyldEnvVar(const char* envVar);
    void                 customizeAddRPath(const char* path);
    void                 customizeAddSegment(const char* segName);
    void*                customizeAddSection(const char* segName, const char* sectionName, uint32_t sectFlags = 0);
    void                 customizeAddZeroFillSection(const char* segName, const char* sectionName);
    void                 customizeAddFunction(const char* functionName, bool global=true);
    void                 customizeAddData(const char* dataName, bool global=true);
    void                 customizeAddZeroFillData(const char* dataName, uint64_t size, bool global=true);
    void                 customizeAddInitializer();

    // does actual build of mach-o image
    const MachOAnalyzer* header();
    const size_t         size() const;
    
    // These methods are for making malformed mach-o files for use in unit-tests
    void                 wrenchRemoveDyld();
    void                 wrenchRemoveInstallName();
    void                 wrenchAddExtraInstallName(const char* path);
    void                 wrenchSetNoDependentDylibs();
    void                 wrenchRemoveUUID();
    void                 wrenchAddUUID();
    void                 wrenchRemoveVersionInfo();
    void                 wrenchAddMain();
    load_command*        wrenchFindLoadCommand(uint32_t cmd);

    // write a tmp file (for debugging MockO)
    void                 save(char savedPath[PATH_MAX]);

private:
    struct PathWithVersions
    {
        std::string              path;
        uint32_t                 compatVers;
        uint32_t                 curVers;
        uint32_t                 cmd;
    };

    struct Content
    {
                                Content(const char* name, bool glob, uint64_t size) : symbolName(name), global(glob), zeroFillSize(size) { }
                                Content(const char* name, bool glob, const std::vector<uint8_t>& b) : symbolName(name), global(glob), bytes(b), zeroFillSize(0) { }
        std::string             symbolName;
        bool                    global;
        std::vector<uint8_t>    bytes;
        uint64_t                zeroFillSize = 0;
    };

    struct SectInfo
    {
                                 SectInfo(const char* nm, uint32_t f) : name(nm), flags(f) { }
        const char*              name;
        uint32_t                 flags;
        std::vector<Content>     content;
        uint64_t                 fileOffset = 0;
        uint64_t                 vmAddr     = 0;
        uint64_t                 size       = 0;
   };

    struct SegInfo
    {
                                 SegInfo(const char* n, uint32_t perm) : name(n), perms(perm) { }
        const char*              name;
        uint32_t                 perms;
        std::vector<SectInfo>    sections;
        uint64_t                 fileOffset = 0;
        uint64_t                 fileSize   = 0;
        uint64_t                 vmAddr     = 0;
        uint64_t                 vmSize     = 0;
    };

    struct Symbol
    {
        std::string              name;
        uint64_t                 vmOffset;
    };

    struct LinkEditLayout
    {
        uint32_t        exportsTrieOffset;
        uint32_t        exportsTrieSize;
        uint32_t        symbolTableOffset;
        uint32_t        symbolTableCount;
        uint32_t        symbolStringsOffset;
        uint32_t        symbolStringsSize;
    };

    struct LinkEditContent
    {
        std::vector<uint8_t>        exportsTrieBytes;
        std::vector<uint32_t>       indirectSymbolTable;
        std::vector<struct nlist>   symbolTable32;
        std::vector<nlist_64>       symbolTable64;
        std::vector<char>           symbolTableStringPool;
    };

    struct PlatformEpoch
    {
        Platform        platform;
        uint32_t        osVersion;
    };

    void                 buildMachO();
    void                 layout();
    void                 writeHeaderAndLoadCommands();
    void                 writeLinkEdit();
    bool                 is64() const;
    load_command*        firstLoadCommand() const;
    load_command*        appendLoadCommand(uint32_t cmd, uint32_t cmdSize);
    void                 appendLoadCommand(const load_command*);
    void                 appendSegmentLoadCommand(const SegInfo&);
    void                 appendPathVersionLoadCommand(const PathWithVersions&);
    void                 appendStringLoadCommand(uint32_t cmd, const std::string&);
    void                 appendSymbolTableLoadCommand();
    void                 appendFixupLoadCommand();
    void                 buildLinkEditContent();
    void                 appendEntryLoadCommand();
    uint32_t             parseVersionNumber32(const char* versionString);
    void                 addUniqueUUID();
    void                 appendRoutinesLoadCommand(uint64_t);
    void                 addBuildVersion(Platform, uint32_t minOS, uint32_t sdk);
    void                 addVersionMin(Platform, uint32_t minOS, uint32_t sdk);
    void                 addSegment(const char* segName, uint32_t perms);
    void                 addSection(const char* segName, const char* sectionName,uint32_t sectFlags = 0);
    SectInfo*            findSection(const char* segName, const char* sectionName);
    bool                 targetIsAtLeast(const PlatformEpoch epoch[]);
    void                 pageAlign(uint64_t& value);
    void                 pageAlign(uint32_t& value);
    uint32_t             alignLC(uint32_t value);

    static const PlatformEpoch epochFall2018[];
    static const PlatformEpoch epochFall2019[];
    static const PlatformEpoch epochFall2020[];
    static const PlatformEpoch epochFall2021[];

    // temp fields used while building mach-o
    mach_header                         _header;
    Platform                            _platform;
    uint32_t                            _minOSVersion;
    uint32_t                            _sdkVersion;
    std::vector<SegInfo>                _segments;
    std::vector<PathWithVersions>       _installName;
    std::vector<version_min_command>    _versionMin;
    std::vector<build_version_command>  _buildVersions;
    std::vector<PathWithVersions>       _dependents;
    std::vector<uuid_command>           _uuid;
    std::vector<std::string>            _dyldEnvVars;
    std::vector<std::string>            _dynamicLinker;
    std::vector<std::string>            _rpaths;
    std::vector<Symbol>                 _exportedSymbols;
    std::vector<Symbol>                 _localSymbols;
    std::optional<dyld_info_command>    _dyldInfo;
    std::optional<uint64_t>             _routinesInitOffset;
    uint64_t                            _baseAddress = 0;
    uint32_t                            _mainOffset = 0;
    LinkEditLayout                      _leLayout;
    LinkEditContent                     _leContent;

    // final mach-o
    MachOAnalyzer*                      _mf   = nullptr;
    size_t                              _size = 0;
};


// A Muckle is larger than a MockO, which conveniently makes it suitable to mock a FAT file
class VIS_HIDDEN Muckle {
public:
    // Methods for building a FAT image in-memory
    // All info is stored in ivars until header() is called.
                    Muckle();
                    ~Muckle();

    void            addMockO(MockO* mock);

    // does actual build of FAT image
    const FatFile*  header();

    // write a tmp file (for debugging MockO)
    void            save(char savedPath[PATH_MAX]);

private:
    void            buildFATFile();

    std::vector<MockO*> _mockos;

    FatFile*            _fat  = nullptr;
    size_t              _size = 0;
};

#endif // MockO_h

