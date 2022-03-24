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

#include <stdio.h>
#include <stdlib.h>
#include <mach-o/loader.h>
#include <mach-o/dyld.h>
#include <mach-o/nlist.h>

#include "Trie.hpp"
#include "MockO.h"

using dyld3::MachOFile;
using dyld3::Platform;
using dyld3::GradedArchs;


//
// MARK: --- methods for configuring a mach-o image ---
//

MockO::MockO(uint32_t filetype, const char* archName, Platform platform, const char* minOsStr, const char* sdkStr)
{
    _header.cputype     = MachOFile::cpuTypeFromArchName(archName);
    _header.cpusubtype  = MachOFile::cpuSubtypeFromArchName(archName);
    _header.magic       = (_header.cputype & CPU_ARCH_ABI64) ? MH_MAGIC_64 : MH_MAGIC;
    _header.filetype    = filetype;
    _header.ncmds       = 0;
    _header.sizeofcmds  = 0;
    _header.flags       = 0;

    if ( filetype == MH_EXECUTE )
        addSegment("__PAGEZERO", 0);

    addSegment("__TEXT",     VM_PROT_READ|VM_PROT_EXECUTE);
    addSegment("__DATA",     VM_PROT_READ|VM_PROT_WRITE);
    addSegment("__LINKEDIT", VM_PROT_READ);
    customizeAddSection("__TEXT", "__text", S_REGULAR);
    customizeAddSection("__DATA", "__data", S_REGULAR);

    _platform     = platform;
    _minOSVersion = 0;
    _sdkVersion   = 0;
    if ( minOsStr != nullptr )
        _minOSVersion = parseVersionNumber32(minOsStr);
    if ( sdkStr != nullptr )
        _sdkVersion = parseVersionNumber32(sdkStr);
    if (_sdkVersion == 0 ) {
        if ( _minOSVersion == 0 ) {
            // if minOS not specified, use defaults for macOS and iOS
            if ( _platform == Platform::macOS )
                _minOSVersion = 0x000C0000;  // default is macOS 12.0
            else if ( _platform == Platform::iOS )
                _minOSVersion = 0x000F0000;  // default is iOS 15.0
            else
                assert(0 && "no default SDK/minOS for platform");
        }
        // if sdk version not specified, use same as minOS version
        _sdkVersion = _minOSVersion;
    }
    if ( _minOSVersion == 0 ) {
        _minOSVersion = _sdkVersion;
    }

    // give dylibs a default install name
    if ( filetype == MH_DYLIB )
        _installName.push_back({"/usr/lib/libfoo.dylib", 1, 1, LC_ID_DYLIB});

    // add platform/minOS info
    if ( targetIsAtLeast(epochFall2018) )
        addBuildVersion(_platform, _minOSVersion, _sdkVersion);
    else
        addVersionMin(_platform, _minOSVersion, _sdkVersion);

    // add a uuid to the binary
    addUniqueUUID();

    if ( filetype == MH_EXECUTE )
       _baseAddress = (_header.cputype & CPU_ARCH_ABI64) ? 0x100000000ULL : 0x1000;

    if ( (filetype == MH_EXECUTE) && (_platform != Platform::driverKit) )
        _mainOffset = 0x1000;

    // make main executable dynamic by default
    if ( filetype == MH_EXECUTE )
        _dynamicLinker.push_back("/usr/lib/dyld");

    // all binaries link with libSystem by default
    if ( _dependents.empty() )
        _dependents.push_back({"/usr/lib/libSystem.B.dylib", 1, 1, LC_LOAD_DYLIB});
}

//
// Parses number of form X[.Y[.Z]] into a uint32_t where the nibbles are xxxx.yy.zz
//
uint32_t MockO::parseVersionNumber32(const char* versionString)
{
	unsigned long x = 0;
	unsigned long y = 0;
	unsigned long z = 0;
	char* end;
	x = strtoul(versionString, &end, 10);
	if ( *end == '.' ) {
		y = strtoul(&end[1], &end, 10);
		if ( *end == '.' ) {
			z = strtoul(&end[1], &end, 10);
		}
	}
	assert((*end == '\0') && (x <= 0xffff) && (y <= 0xff) && (z <= 0xff) && "malformed 32-bit x.y.z version number");
	return (uint32_t)((x << 16) | ( y << 8 ) | z);
}


void MockO::customizeMakeZippered()
{
    addBuildVersion(Platform::iOSMac, 0x000E0000, 0x000E0000);
}

void MockO::customizeInstallName(const char* path, uint32_t compatVers, uint32_t curVers)
{
    assert(_header.filetype == MH_DYLIB);
    assert(_installName.size() == 1);
    _installName[0] = { path, compatVers, curVers, LC_ID_DYLIB };
}

void MockO::customizeAddDependentDylib(const char* path, bool isWeak, bool isUpward, bool isReexport, uint32_t compatVers, uint32_t curVers)
{
    PathWithVersions pv;
    pv.path       = path;
    pv.compatVers = compatVers;
    pv.curVers    = curVers;
    if ( isWeak )
        pv.cmd    = LC_LOAD_WEAK_DYLIB;
    else if ( isReexport )
        pv.cmd    = LC_REEXPORT_DYLIB;
    else if ( isUpward )
        pv.cmd    = LC_LOAD_UPWARD_DYLIB;
    else
        pv.cmd    = LC_LOAD_DYLIB;

    _dependents.push_back(pv);
}

void MockO::customizeAddDyldEnvVar(const char* envVar)
{
    _dyldEnvVars.push_back(envVar);
}

void MockO::customizeAddRPath(const char* path)
{
    _rpaths.push_back(path);
}

MockO::SectInfo* MockO::findSection(const char* segName, const char* sectionName)
{
    for (SegInfo& segInfo : _segments) {
        if ( strcmp(segInfo.name, segName) == 0 ) {
            for (SectInfo& sectInfo : segInfo.sections) {
                if ( strcmp(sectInfo.name, sectionName) == 0 ) {
                    return &sectInfo;
                }
            }
        }
    }
    return nullptr;
}

void MockO::customizeAddSegment(const char* segName)
{
    addSegment(segName, VM_PROT_READ);
}

void* MockO::customizeAddSection(const char* segName, const char* sectionName, uint32_t sectFlags)
{
   for (SegInfo& segInfo : _segments) {
        if ( strcmp(segInfo.name, segName) == 0 ) {
            segInfo.sections.emplace_back(sectionName, sectFlags);
        }
    }
    return nullptr;
}

void MockO::customizeAddZeroFillSection(const char* segName, const char* sectionName)
{
   for (SegInfo& segInfo : _segments) {
        if ( strcmp(segInfo.name, segName) == 0 ) {
            segInfo.sections.emplace_back(sectionName, S_ZEROFILL);
        }
    }
}

void MockO::customizeAddFunction(const char* functionName, bool global)
{
    SectInfo* text = this->findSection("__TEXT", "__text");
    assert(text != nullptr);
    const std::vector<uint8_t> bytes = {0x90, 0x90, 0x90, 0x90};
    text->content.emplace_back(functionName, global, bytes);
}

void MockO::customizeAddData(const char* dataName, bool global)
{
    SectInfo* data = this->findSection("__DATA", "__data");
    assert(data != nullptr);
    const std::vector<uint8_t> bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    data->content.emplace_back(dataName, global, bytes);
}

void MockO::customizeAddZeroFillData(const char* dataName, uint64_t size, bool global)
{
    if ( global ) {
        SectInfo* data = this->findSection("__DATA", "__common");
        if ( data == nullptr ) {
            customizeAddZeroFillSection("__DATA", "__common");
            data = this->findSection("__DATA", "__common");
        }
        data->content.emplace_back(dataName, true, size);
    }
    else {
        SectInfo* bss = this->findSection("__DATA", "__bss");
        if ( bss == nullptr ) {
            customizeAddZeroFillSection("__DATA", "__bss");
            bss = this->findSection("__DATA", "__bss");
        }
        bss->content.emplace_back(dataName, false, size);
    }
}

void MockO::customizeAddInitializer()
{
    const std::vector<uint8_t> bytes4 = { 0, 0, 0, 0 };
    const std::vector<uint8_t> bytes8 = { 0, 0, 0, 0, 0, 0, 0, 0 };
    customizeAddFunction("myinit", false);
    if ( targetIsAtLeast(epochFall2021) ) {
        customizeAddSection("__TEXT", "__init_offsets", S_INIT_FUNC_OFFSETS);
        SectInfo* sect = this->findSection("__TEXT", "__init_offsets");
        sect->content.emplace_back("", false, bytes4);
    }
    else {
        customizeAddSection("__DATA", "__mod_init_func", S_MOD_INIT_FUNC_POINTERS);
        SectInfo* sect = this->findSection("__DATA", "__mod_init_func");
        if ( is64() )
            sect->content.emplace_back("", false, bytes8);
        else
            sect->content.emplace_back("", false, bytes4);
   }
    // FIXME
}



void MockO::addSegment(const char* segName, uint32_t perms)
{
    _segments.emplace_back(segName, perms);
}

void MockO::addSection(const char* segName, const char* sectionName, uint32_t sectFlags)
{
    for (SegInfo& info : _segments) {
        if ( strcmp(info.name, segName) == 0 ) {
            info.sections.emplace_back(sectionName, sectFlags);
            return;
        }
    }
    assert(0 && "segment not found");
}



#if 0
uint64_t MockO::getSegmentSize(const char* segName)
{
    for (SegInfo& info : _segments) {
        if ( strcmp(info.name, segName) == 0 ) {
            return info.size;
        }
    }
    assert(0 && "segment not found");
}

void MockO::setSegmentPerm(const char* segName, uint32_t protections)
{
    for (SegInfo& info : _segments) {
        if ( strcmp(info.name, segName) == 0 ) {
            info.perms = protections;
            return;
        }
    }
    assert(0 && "segment not found");
}

uint64_t MockO::getSegmentOffset(const char* segName)
{
    for (SegInfo& info : _segments) {
        if ( strcmp(info.name, segName) == 0 ) {
            return info.fileOffset;
        }
    }
    assert(0 && "segment not found");
}

void MockO::setSegmentOffset(const char* segName, uint64_t offset)
{
    for (SegInfo& info : _segments) {
        if ( strcmp(info.name, segName) == 0 ) {
            info.fileOffset = offset;
            return;
        }
    }
    assert(0 && "segment not found");
}



void MockO::setSectionOffset(const char* segName, const char* sectionName, uint64_t offset)
{
    for (SegInfo& segInfo : _segments) {
        if ( strcmp(segInfo.name, segName) == 0 ) {
            for (SectInfo& sectInfo : segInfo.sections) {
                if ( strcmp(sectInfo.name, sectionName) == 0 )
                    sectInfo.fileOffset = offset;
            }
            return;
        }
    }
    assert(0 && "section not found");
}
#endif

bool MockO::is64() const
{
    return (_header.magic == MH_MAGIC_64);
}


void MockO::addUniqueUUID()
{
    uuid_t aUUID;
    uuid_generate_random(aUUID);
    uuid_command uc;
    uc.cmd      = LC_UUID;
    uc.cmdsize  = sizeof(uuid_command);
    memcpy(uc.uuid, aUUID, sizeof(uuid_t));
    _uuid.push_back(uc);
}

void MockO::addBuildVersion(Platform platform, uint32_t minOS, uint32_t sdk)
{
    build_version_command bv;
    bv.cmd      = LC_BUILD_VERSION;
    bv.cmdsize  = sizeof(build_version_command);
    bv.platform = (uint32_t)platform;
    bv.minos    = minOS;
    bv.sdk      = sdk;
    bv.ntools   = 0;
    _buildVersions.push_back(bv);
}

void MockO::addVersionMin(Platform platform, uint32_t minOS, uint32_t sdk)
{
    version_min_command vc;
    vc.cmdsize = sizeof(version_min_command);
    vc.version = minOS;
    vc.sdk     = sdk;
    switch (platform) {
        case Platform::macOS:
            vc.cmd = LC_VERSION_MIN_MACOSX;
            break;
        case Platform::iOS:
            vc.cmd = LC_VERSION_MIN_IPHONEOS;
            break;
        case Platform::watchOS:
            vc.cmd = LC_VERSION_MIN_WATCHOS;
            break;
        case Platform::tvOS:
            vc.cmd = LC_VERSION_MIN_TVOS;
            break;
        default:
            assert(0 && "invalid platform for min version load command");
    }
    _versionMin.push_back(vc);
}

#define VERS(major, minor) ((((major)&0xffff) << 16) | (((minor)&0xff) << 8))

// Fall 2018 introduced LC_BUILD_VERSION
const MockO::PlatformEpoch MockO::epochFall2018[] = {
    { Platform::macOS,   VERS(10,14) },
    { Platform::iOS,     VERS(12,0)  },
    { Platform::watchOS, VERS( 5,0)  },
    { Platform::tvOS,    VERS(12,0)  },
    { Platform::unknown, VERS( 0,0)  }  // zero terminate
};

// Fall 2019 introduced __DATA_CONST and zippering
const MockO::PlatformEpoch MockO::epochFall2019[] = {
    { Platform::macOS,   VERS(10,15) },
    { Platform::iOS,     VERS(13,0)  },
    { Platform::watchOS, VERS( 6,0)  },
    { Platform::tvOS,    VERS(13,0)  },
    { Platform::unknown, VERS( 0,0)  }  // zero terminate
};

// Fall 2020 introduced relative method lists
const MockO::PlatformEpoch MockO::epochFall2020[] = {
    { Platform::macOS,     VERS(11,0) },
    { Platform::iOS,       VERS(14,0) },
    { Platform::watchOS,   VERS( 7,0) },
    { Platform::tvOS,      VERS(14,0) },
    { Platform::driverKit, VERS(20,0) },
    { Platform::unknown,   VERS( 0,0) }  // zero terminate
};

// Fall 2021 introduced chained fixups and initializer offsets
const MockO::PlatformEpoch MockO::epochFall2021[] = {
    { Platform::macOS,     VERS(12,0) },
    { Platform::iOS,       VERS(15,0) },
    { Platform::watchOS,   VERS( 8,0) },
    { Platform::tvOS,      VERS(15,0) },
    { Platform::driverKit, VERS(21,0) },
    { Platform::unknown,   VERS( 0,0) }  // zero terminate
};


bool MockO::targetIsAtLeast(const PlatformEpoch epoch[])
{
    for (const PlatformEpoch* e=epoch; e->osVersion != 0; ++e) {
        if ( _platform == e->platform ) {
            return ( _minOSVersion >= e->osVersion );
        }
    }
    return true;
}




//
// MARK: --- methods for malforming mach-o ---
//

void MockO::wrenchRemoveDyld()
{
    _dynamicLinker.clear();
}

void MockO::wrenchRemoveInstallName()
{
    _installName.clear();
}

void MockO::wrenchAddExtraInstallName(const char* path)
{
    _installName.push_back({ path, 1, 1, LC_ID_DYLIB });
}

void MockO::wrenchSetNoDependentDylibs()
{
    _dependents.clear();
}

void MockO::wrenchRemoveUUID()
{
    _uuid.clear();
}

void MockO::wrenchAddUUID()
{
    addUniqueUUID();
}

void MockO::wrenchRemoveVersionInfo()
{
    _versionMin.clear();
    _buildVersions.clear();
}

void MockO::wrenchAddMain()
{
    _mainOffset = 0x1000;
}

load_command* MockO::wrenchFindLoadCommand(uint32_t cmd)
{
    const MachOAnalyzer*  ma     = header();
    __block load_command* result = nullptr;
    Diagnostics           diag;
    ma->forEachLoadCommand(diag, ^(const load_command* lc, bool& stop) {
        if ( lc->cmd == cmd ) {
            result = (load_command*)lc;
            stop = true;
        }
    });
    return result;
}


//
// MARK: --- methods for building actual mach-o image ---
//

uint32_t MockO::alignLC(uint32_t value)
{
    // mach-o requires all load command sizes to be a multiple the pointer size
    if ( is64() )
        return ((value + 7) & (-8));
    else
        return ((value + 3) & (-4));
}


MockO::~MockO()
{
}

const MachOAnalyzer* MockO::header()
{
    if ( _mf == nullptr )
        buildMachO();

    return _mf;
}

const size_t MockO::size() const
{
    return _size;
}

void MockO::save(char savedPath[PATH_MAX])
{
    const MachOAnalyzer* ma = this->header();

    ::strcpy(savedPath, "/tmp/mocko-XXXXXX");
    int fd = ::mkstemp(savedPath);
    ::pwrite(fd, ma, _size, 0);
    ::close(fd);
}

void MockO::buildMachO()
{
    // assign addresses/offsets to segments, sections, symbols
    layout();

    // allocate space for mach-o image
    assert(::vm_allocate(mach_task_self(), (vm_address_t*)&_mf, (vm_size_t)_size, VM_FLAGS_ANYWHERE) == KERN_SUCCESS);


    writeHeaderAndLoadCommands();

    writeLinkEdit();
}


void MockO::writeHeaderAndLoadCommands()
{
    // copy header
    *((mach_header*)_mf) = _header;

    // add segment load commands
    for (const SegInfo& info : _segments)
        appendSegmentLoadCommand(info);

    // add fixup info
    appendFixupLoadCommand();

    // add nlist symbol table
    appendSymbolTableLoadCommand();

    // if set, add install name
    for ( const PathWithVersions& install : _installName )
        appendPathVersionLoadCommand(install);

    // add dyld load command
    for ( const std::string& pth : _dynamicLinker )
        appendStringLoadCommand(LC_LOAD_DYLINKER,pth);

    // add uuid
    for ( const uuid_command& id : _uuid )
        appendLoadCommand((load_command*)&id);

    // if set, add version-min load command
    for ( const version_min_command& vc : _versionMin )
        appendLoadCommand((load_command*)&vc);

    // add build-version load command(s)
    for (const build_version_command& bv : _buildVersions)
        appendLoadCommand((load_command*)&bv);

    // add entry
    if ( (_header.filetype == MH_EXECUTE) && (_mainOffset != 0) )
        appendEntryLoadCommand();

    // add dyld info
    if ( _dyldInfo.has_value() )
        appendLoadCommand((load_command*)&_dyldInfo.value());

    if ( _routinesInitOffset )
        appendRoutinesLoadCommand(_routinesInitOffset.value());


    // add dependent dylibs
    for (const PathWithVersions& dep : _dependents)
        appendPathVersionLoadCommand(dep);

    // add any dyld env var load commands
    for (const std::string& str : _dyldEnvVars)
        appendStringLoadCommand(LC_DYLD_ENVIRONMENT, str);

    // add any rpath load commands
    for (const std::string& str : _rpaths)
        appendStringLoadCommand(LC_RPATH, str);

}

static int segmentOrder(const char* name)
{
    if ( strcmp(name, "__PAGEZERO") == 0 )
        return 1;
    if ( strcmp(name, "__TEXT") == 0 )
        return 2;
    if ( strcmp(name, "__DATA_CONST") == 0 )
        return 3;
    if ( strcmp(name, "__DATA") == 0 )
        return 4;
    if ( strcmp(name, "__LINKEDIT") == 0 )
        return 999;
    return 10;
}

void MockO::pageAlign(uint64_t& value)
{
    // FIXME: is there any case where we want 4KB pages?
    value = ((value + 0x3FFF) & (-0x4000));
}

void MockO::pageAlign(uint32_t& value)
{
    // FIXME: is there any case where we want 4KB pages?
    value = ((value + 0x3FFF) & (-0x4000));
}

void MockO::layout()
{
    // sort segments
    std::sort(_segments.begin(), _segments.end(), [](const SegInfo& s1, const SegInfo& s2) {
        return segmentOrder(s1.name) < segmentOrder(s2.name);
    });

    // assign address to segments
    SegInfo* leSegInfo     = nullptr;
    uint32_t curFileOffset = 0;
    uint64_t curVMAddr     = 0;
    for (SegInfo& segInfo : _segments) {
        pageAlign(curFileOffset);
        pageAlign(curVMAddr);
        segInfo.fileOffset = curFileOffset;
        segInfo.vmAddr     = curVMAddr;
        if ( strcmp(segInfo.name, "__TEXT") == 0 ) {
            curVMAddr = _baseAddress;
            segInfo.fileOffset = 0;
            segInfo.vmAddr     = curVMAddr;
            // reverse layout TEXT so padding is after load commands and before __text
            uint64_t totalSectionsSize = 0;
            for (SectInfo& sectInfo : segInfo.sections) {
                sectInfo.size = 0;
                for (const Content& cont : sectInfo.content) {
                    sectInfo.size += cont.bytes.size();
                }
                totalSectionsSize += sectInfo.size;
            }
            uint32_t textPageSize = (uint32_t)totalSectionsSize+2048; // FIXME: guestimate of load commands size
            pageAlign(textPageSize);
            segInfo.fileSize   = textPageSize;
            segInfo.vmSize     = textPageSize;
            uint64_t addr        = segInfo.vmAddr + segInfo.vmSize;
            uint64_t off         = textPageSize;
            for (int i=(int)segInfo.sections.size(); i > 0; --i) {
                SectInfo& sectInfo = segInfo.sections[i-1];
                addr -= sectInfo.size;
                off  -= sectInfo.size;
                uint64_t symAddr = addr;
                for (const Content& cont : sectInfo.content) {
                    if ( cont.symbolName[0] != '\0' ) {
                        if ( cont.global )
                            _exportedSymbols.push_back({cont.symbolName, symAddr});
                        else
                            _localSymbols.push_back({cont.symbolName, symAddr});
                    }
                    symAddr += cont.bytes.size();
                }
                sectInfo.vmAddr     = addr;
                sectInfo.fileOffset = off;
            }
            curFileOffset = textPageSize;
            curVMAddr     = segInfo.vmAddr + segInfo.vmSize;
        }
        else if ( strcmp(segInfo.name, "__PAGEZERO") == 0 ) {
            segInfo.fileOffset = 0;
            segInfo.fileSize   = 0;
            segInfo.vmAddr     = 0;
            segInfo.vmSize     = _baseAddress;
        }
        else if ( strcmp(segInfo.name, "__LINKEDIT") == 0 ) {
            // LINKEDIT size set later
            leSegInfo = &segInfo;
        }
        else {
            // sort sections so zero-fill at end
            std::sort(segInfo.sections.begin(), segInfo.sections.end(), [](const SectInfo& l, const SectInfo& r) {
                bool lzf = (l.flags == S_ZEROFILL);
                bool rzf = (r.flags == S_ZEROFILL);
                if ( lzf != rzf )
                    return rzf;
                return (strcmp(l.name, r.name) == 0);
            });
            for (SectInfo& sectInfo : segInfo.sections) {
                sectInfo.fileOffset = curFileOffset;
                sectInfo.vmAddr     = curVMAddr;
                sectInfo.size       = 0;
                uint64_t symVmOffset = curVMAddr - _baseAddress;
                if ( sectInfo.flags == S_ZEROFILL ) {
                    sectInfo.fileOffset = 0; // all zero-fill sections have no file offset
                    for (const Content& cont : sectInfo.content) {
                        assert(cont.zeroFillSize != 0);
                        assert(cont.bytes.empty());
                        if ( cont.symbolName[0] != '\0' ) {
                            if ( cont.global )
                                _exportedSymbols.push_back({cont.symbolName, symVmOffset});
                            else
                                _localSymbols.push_back({cont.symbolName, symVmOffset});
                        }
                        sectInfo.size  += cont.zeroFillSize;
                        segInfo.vmSize += cont.zeroFillSize;
                        symVmOffset    += cont.zeroFillSize;
                        curVMAddr      += cont.zeroFillSize;
                    }
                }
                else {
                    for (const Content& cont : sectInfo.content) {
                        assert(cont.zeroFillSize == 0);
                        assert(!cont.bytes.empty());
                        // FIXME: support alignment
                        if ( cont.symbolName[0] != '\0' ) {
                            if ( cont.global )
                                _exportedSymbols.push_back({cont.symbolName, symVmOffset});
                            else
                                _localSymbols.push_back({cont.symbolName, symVmOffset});
                        }
                        sectInfo.size    += cont.bytes.size();
                        segInfo.fileSize += sectInfo.size;
                        segInfo.vmSize   += sectInfo.size;
                        curVMAddr        += sectInfo.size;
                        curFileOffset    += sectInfo.size;
                   }
                }
            }
            pageAlign(segInfo.fileSize);
            pageAlign(segInfo.vmSize);
        }
    }

    // layout linkedit

    // build exports trie
    std::vector<ExportInfoTrie::Entry> trieEntrys;
    for (const Symbol& exp : _exportedSymbols) {
        ExportInfoTrie::Entry entry;
        entry.name            = exp.name;
        entry.info.address    = exp.vmOffset;
        entry.info.flags      = 0; // FIXME
        entry.info.other      = 0;
        entry.info.importName = "";
        trieEntrys.push_back(entry);
    }
    ExportInfoTrie programTrie(trieEntrys);
    programTrie.emit(_leContent.exportsTrieBytes);
    while ( (_leContent.exportsTrieBytes.size() % 8) != 0 )
        _leContent.exportsTrieBytes.push_back(0);
    _leLayout.exportsTrieOffset = curFileOffset;
    _leLayout.exportsTrieSize   = (uint32_t)_leContent.exportsTrieBytes.size();
    curFileOffset += _leLayout.exportsTrieSize;

    // nlist symbol table
    _leLayout.symbolTableOffset   = curFileOffset;
    _leLayout.symbolTableCount    = (uint32_t)(_exportedSymbols.size() + _localSymbols.size());
    _leContent.symbolTableStringPool.push_back('\0');
    for (const Symbol& exp : _localSymbols) {
        if ( is64() ) {
            nlist_64 sym;
            sym.n_un.n_strx = (uint32_t)_leContent.symbolTableStringPool.size();
            sym.n_type      = N_SECT;
            sym.n_sect      = 1; // FIXME
            sym.n_desc      = 0;
            sym.n_value     = exp.vmOffset;
            _leContent.symbolTable64.push_back(sym);
        }
        else {
            struct nlist sym;
            sym.n_un.n_strx = (uint32_t)_leContent.symbolTableStringPool.size();
            sym.n_type      = N_SECT;
            sym.n_sect      = 1; // FIXME
            sym.n_desc      = 0;
            sym.n_value     = (uint32_t)exp.vmOffset;
            _leContent.symbolTable32.push_back(sym);
        }
        const char* str = exp.name.c_str();
        _leContent.symbolTableStringPool.insert(_leContent.symbolTableStringPool.end(), str, str+strlen(str)+1);
    }
    for (const Symbol& exp : _exportedSymbols) {
        if ( is64() ) {
            nlist_64 sym;
            sym.n_un.n_strx = (uint32_t)_leContent.symbolTableStringPool.size();
            sym.n_type      = N_SECT | N_EXT;;
            sym.n_sect      = 1; // FIXME
            sym.n_desc      = 0;
            sym.n_value     = exp.vmOffset;
            _leContent.symbolTable64.push_back(sym);
        }
        else {
            struct nlist sym;
            sym.n_un.n_strx = (uint32_t)_leContent.symbolTableStringPool.size();
            sym.n_type      = N_SECT | N_EXT;;
            sym.n_sect      = 1; // FIXME
            sym.n_desc      = 0;
            sym.n_value     = (uint32_t)exp.vmOffset;
            _leContent.symbolTable32.push_back(sym);
        }
        const char* str = exp.name.c_str();
        _leContent.symbolTableStringPool.insert(_leContent.symbolTableStringPool.end(), str, str+strlen(str)+1);
    }
    while ( (_leContent.symbolTableStringPool.size() % 8) != 0 )
        _leContent.symbolTableStringPool.push_back('\0');

    curFileOffset += _leLayout.symbolTableCount * (is64() ? sizeof(nlist_64) : sizeof(struct nlist));
    _leLayout.symbolStringsOffset = curFileOffset;
    _leLayout.symbolStringsSize   = (uint32_t)_leContent.symbolTableStringPool.size();
    curFileOffset += _leLayout.symbolStringsSize;

    _size = curFileOffset;

    leSegInfo->fileSize = curFileOffset - leSegInfo->fileOffset;
    leSegInfo->vmSize   = leSegInfo->fileSize;
    pageAlign(leSegInfo->vmSize);
}


void MockO::writeLinkEdit()
{
    // write exports trie
    ::memcpy((char*)_mf + _leLayout.exportsTrieOffset, &_leContent.exportsTrieBytes[0], _leLayout.exportsTrieSize);


    // write symbol table
    if ( is64() )
        ::memcpy((char*)_mf + _leLayout.symbolTableOffset, &_leContent.symbolTable64[0], _leLayout.symbolTableCount*sizeof(nlist_64));
    else
        ::memcpy((char*)_mf + _leLayout.symbolTableOffset, &_leContent.symbolTable32[0], _leLayout.symbolTableCount*sizeof(struct nlist));

    // write symbol table string pool
    ::memcpy((char*)_mf + _leLayout.symbolStringsOffset, &_leContent.symbolTableStringPool[0], _leLayout.symbolStringsSize);

}

load_command* MockO::firstLoadCommand() const
{
    if ( _mf->magic == MH_MAGIC_64 )
        return (load_command*)((uint8_t*)_mf + sizeof(mach_header_64));
    else if ( _mf->magic == MH_MAGIC )
        return (load_command*)((uint8_t*)_mf + sizeof(mach_header));
    assert(0 && "no mach-o magic");
}

// creates space for a new load command, but does not fill in its payload
load_command* MockO::appendLoadCommand(uint32_t cmd, uint32_t cmdSize)
{
    assert(cmdSize == alignLC(cmdSize)); // size must be multiple of 4
    load_command* thisCmd = (load_command*)((uint8_t*)firstLoadCommand() + _mf->sizeofcmds);
    thisCmd->cmd     = cmd;
    thisCmd->cmdsize = cmdSize;
    _mf->ncmds       += 1;
    _mf->sizeofcmds  += cmdSize;

    return thisCmd;
}

// copies a new load command from another
void MockO::appendLoadCommand(const load_command* lc)
{
    assert(lc->cmdsize == alignLC(lc->cmdsize)); // size must be multiple of 4
    load_command* thisCmd = (load_command*)((uint8_t*)firstLoadCommand() + _mf->sizeofcmds);
    ::memcpy(thisCmd, lc, lc->cmdsize);
    _mf->ncmds       += 1;
    _mf->sizeofcmds  += lc->cmdsize;
}

void MockO::appendEntryLoadCommand()
{
    // FIXME: old macOS binaries use different load command
    entry_point_command* mlc = (entry_point_command*)appendLoadCommand(LC_MAIN, sizeof(entry_point_command));
    mlc->entryoff  = _mainOffset;
    mlc->stacksize = 0;
}

void MockO::appendFixupLoadCommand()
{
    if ( targetIsAtLeast(epochFall2021) ) {
        linkedit_data_command* etlc = (linkedit_data_command*)appendLoadCommand(LC_DYLD_EXPORTS_TRIE, sizeof(linkedit_data_command));
        etlc->dataoff   = _leLayout.exportsTrieOffset;
        etlc->datasize  = _leLayout.exportsTrieSize;
    }
    else {
        dyld_info_command* dilc = (dyld_info_command*)appendLoadCommand(LC_DYLD_INFO_ONLY, sizeof(dyld_info_command));
        dilc->rebase_off        = 0;
        dilc->rebase_size       = 0;
        dilc->bind_off          = 0;
        dilc->bind_size         = 0;
        dilc->weak_bind_off     = 0;
        dilc->weak_bind_size    = 0;
        dilc->lazy_bind_off     = 0;
        dilc->lazy_bind_size    = 0;
        dilc->export_off        = _leLayout.exportsTrieOffset;
        dilc->export_size       = _leLayout.exportsTrieSize;;
    }

}


void MockO::appendSymbolTableLoadCommand()
{
    symtab_command* stlc = (symtab_command*)appendLoadCommand(LC_SYMTAB, sizeof(symtab_command));
    stlc->symoff  = _leLayout.symbolTableOffset;
    stlc->nsyms   = _leLayout.symbolTableCount;
    stlc->stroff  = _leLayout.symbolStringsOffset;
    stlc->strsize = _leLayout.symbolStringsSize;

    dysymtab_command* dlc = (dysymtab_command*)appendLoadCommand(LC_DYSYMTAB, sizeof(dysymtab_command));
    dlc->ilocalsym      = 0;
    dlc->nlocalsym      = (uint32_t)_localSymbols.size();
    dlc->iextdefsym     = dlc->nlocalsym;
    dlc->nextdefsym     = (uint32_t)_exportedSymbols.size();
    dlc->iundefsym      = 0;
    dlc->nundefsym      = 0;
    dlc->tocoff         = 0;
    dlc->ntoc           = 0;
    dlc->modtaboff      = 0;
    dlc->nmodtab        = 0;
    dlc->extrefsymoff   = 0;
    dlc->nextrefsyms    = 0;
    dlc->indirectsymoff = 0;
    dlc->nindirectsyms  = 0;
    dlc->extreloff      = 0;
    dlc->nextrel        = 0;
    dlc->locreloff      = 0;
    dlc->nlocrel        = 0;
}

void MockO::appendPathVersionLoadCommand(const PathWithVersions& pv)
{
    uint32_t cmdSize = alignLC((uint32_t)(sizeof(dylib_command)+pv.path.size()+1));
    dylib_command* lc = (dylib_command*)appendLoadCommand(pv.cmd, cmdSize);
    lc->dylib.name.offset           = sizeof(dylib_command);
    lc->dylib.timestamp             = 1;
    lc->dylib.current_version       = pv.curVers;
    lc->dylib.compatibility_version = pv.compatVers;
    strcpy((char*)lc + sizeof(dylib_command), pv.path.c_str());
}


void MockO::appendSegmentLoadCommand(const SegInfo& info)
{
    if ( is64() ) {
        uint32_t cmdSize = (uint32_t)(sizeof(segment_command_64)+info.sections.size()*sizeof(section_64));
        segment_command_64* newCmd = (segment_command_64*)appendLoadCommand(LC_SEGMENT_64, cmdSize);
        strlcpy(newCmd->segname, info.name, 16);
        newCmd->vmaddr   = info.vmAddr;
        newCmd->vmsize   = info.vmSize;
        newCmd->fileoff  = info.fileOffset;
        newCmd->filesize = info.fileSize;
        newCmd->maxprot  = info.perms;
        newCmd->initprot = info.perms;
        newCmd->nsects   = (uint32_t)info.sections.size();
        newCmd->flags    = 0;
        section_64* sect = (section_64*)((uint8_t*)newCmd + sizeof(segment_command_64));
        for (const SectInfo& sectInfo : info.sections) {
            size_t sectSize = 0;
            for (const Content& cont : sectInfo.content) {
                sectSize += cont.bytes.size();
            }
            strlcpy(sect->sectname, sectInfo.name, 17); // section name can be 16 chars, may overflow into segname, but about to set that
            strlcpy(sect->segname, info.name, 16);
            sect->addr   = sectInfo.vmAddr;
            sect->size   = sectInfo.size;
            sect->offset = (uint32_t)sectInfo.fileOffset;
            sect->flags  = sectInfo.flags;
            ++sect;
        }
    }
    else {
        uint32_t cmdSize = (uint32_t)(sizeof(segment_command)+info.sections.size()*sizeof(section));
        segment_command* newCmd = (segment_command*)appendLoadCommand(LC_SEGMENT, cmdSize);
        strlcpy(newCmd->segname, info.name, 16);
        newCmd->vmaddr   = (uint32_t)info.vmAddr;
        newCmd->vmsize   = (uint32_t)info.vmSize;
        newCmd->fileoff  = (uint32_t)info.fileOffset;
        newCmd->filesize = (uint32_t)info.fileSize;
        newCmd->maxprot  = info.perms;
        newCmd->initprot = info.perms;
        newCmd->nsects   = (uint32_t)info.sections.size();
        newCmd->flags    = 0;
        section* sect = (section*)((uint8_t*)newCmd + sizeof(segment_command));
        for (const SectInfo& sectInfo : info.sections) {
            size_t sectSize = 0;
            for (const Content& cont : sectInfo.content)
                sectSize += cont.bytes.size();
            strlcpy(sect->sectname, sectInfo.name, 17); // section name can be 16 chars, may overflow into segname, but about to set that
            strlcpy(sect->segname, info.name, 16);
            sect->addr  = newCmd->vmaddr;
            sect->size  = (uint32_t)sectSize;
            sect->offset = (uint32_t)sectInfo.fileOffset;
            sect->flags = sectInfo.flags;
           ++sect;
        }
    }
}

void MockO::appendStringLoadCommand(uint32_t cmd, const std::string& str)
{
    uint32_t size = alignLC((uint32_t)(sizeof(dylinker_command)+str.size()+1));
    dylinker_command* newCmd = (dylinker_command*)appendLoadCommand(cmd, size);
    newCmd->name.offset = sizeof(dylinker_command);
    strcpy((char*)newCmd + newCmd->name.offset, str.c_str());
}

void MockO::appendRoutinesLoadCommand(uint64_t offset)
{
    if ( is64() ) {
        uint32_t cmdSize = sizeof(routines_command_64);
        routines_command_64* newCmd = (routines_command_64*)appendLoadCommand(LC_ROUTINES_64, cmdSize);
        newCmd->init_address = offset;
        newCmd->init_module = 0;
        newCmd->reserved1 = 0;
        newCmd->reserved2 = 0;
        newCmd->reserved3 = 0;
        newCmd->reserved4 = 0;
        newCmd->reserved5 = 0;
        newCmd->reserved6 = 0;
    }
    else {
        uint32_t cmdSize = sizeof(routines_command);
        routines_command* newCmd = (routines_command*)appendLoadCommand(LC_ROUTINES, cmdSize);
        newCmd->init_address = (uint32_t)offset;
        newCmd->init_module = 0;
        newCmd->reserved1 = 0;
        newCmd->reserved2 = 0;
        newCmd->reserved3 = 0;
        newCmd->reserved4 = 0;
        newCmd->reserved5 = 0;
        newCmd->reserved6 = 0;
    }
}


//
// MARK: --- methods for configuring a FAT image ---
//

Muckle::Muckle() { }

Muckle::~Muckle() { }

void Muckle::addMockO(MockO *mock)
{
    _mockos.push_back(mock);
}

const FatFile* Muckle::header()
{
    if ( _fat == nullptr )
        buildFATFile();

    return _fat;
}

static uint32_t alignPage(uint32_t value)
{
    return ((value + 16383) & (-16384));
}

void Muckle::buildFATFile()
{
    // Add a page for the FAT header
    _size = 16384;

    for ( MockO* mock : _mockos ) {
        // Force the MockO to build
        mock->header();
        _size += alignPage((uint32_t)mock->size());
    }

    vm_address_t loadAddress = 0;
    assert(::vm_allocate(mach_task_self(), &loadAddress, (vm_size_t)_size, VM_FLAGS_ANYWHERE) == KERN_SUCCESS);
    _fat = (FatFile*)loadAddress;

    // Add the FAT header to the start of the buffer
    fat_header* header = (fat_header*)_fat;
    header->magic       = OSSwapHostToBigInt32(FAT_MAGIC);
    header->nfat_arch   = OSSwapHostToBigInt32((uint32_t)_mockos.size());

    size_t offsetInBuffer = 16384;
    for (uint32_t i = 0; i != _mockos.size(); ++i) {
        MockO* mock = _mockos[i];
        const MachOAnalyzer* ma = mock->header();

        fat_arch* archBuffer = (fat_arch*)((uint8_t*)header + sizeof(fat_header));
        archBuffer[i].cputype       = OSSwapHostToBigInt32(ma->cputype);
        archBuffer[i].cpusubtype    = OSSwapHostToBigInt32(ma->cpusubtype);
        archBuffer[i].offset        = OSSwapHostToBigInt32(offsetInBuffer);
        archBuffer[i].size          = OSSwapHostToBigInt32(mock->size());
        archBuffer[i].align         = OSSwapHostToBigInt32(14);

        uint32_t alignedSize = alignPage((uint32_t)mock->size());
        memcpy((uint8_t*)_fat + offsetInBuffer, ma, mock->size());

        offsetInBuffer += alignedSize;
        assert(offsetInBuffer <= _size);
    }
}

void Muckle::save(char savedPath[PATH_MAX])
{
    const FatFile* ff = this->header();

    ::strcpy(savedPath, "/tmp/muckle-XXXXXX");
    int fd = ::mkstemp(savedPath);
    ::pwrite(fd, ff, _size, 0);
    ::close(fd);
}
