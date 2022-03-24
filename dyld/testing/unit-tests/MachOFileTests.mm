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

#include <mach-o/dyld.h>
#include <mach-o/dyld_priv.h>
#include <dyld/for_dyld_priv.inc>   // this is already incoporated into dyld_priv.h in the SDK
#include <stdio.h>
#include <uuid/uuid.h>

#include <vector>
#include <string>

#include <XCTest/XCTest.h>

#include "DyldRuntimeState.h"
#include "DyldProcessConfig.h"
#include "DyldAPIs.h"
#include "MockO.h"

using dyld4::RuntimeState;
using dyld4::KernelArgs;
using dyld4::ProcessConfig;
using dyld4::APIs;
using dyld3::Platform;


@interface MachOFileTests : XCTestCase
@end

@implementation MachOFileTests


- (void)test_isType
{
    // Arrange: make mach-o files with different file types
    MockO mock1(MH_EXECUTE,     "arm64");
    MockO mock2(MH_DYLIB,       "arm64");
    MockO mock3(MH_BUNDLE,      "arm64");
    MockO mock4(MH_DYLINKER,    "arm64");
    MockO mock5(MH_KEXT_BUNDLE, "arm64");
    MockO mock6(MH_FILESET,     "arm64");

    // Act: test isDylib()
    bool isDylib1  = mock1.header()->isDylib();
    bool isDylib2  = mock2.header()->isDylib();
    bool isDylib3  = mock3.header()->isDylib();
    bool isDylib4  = mock4.header()->isDylib();
    bool isDylib5  = mock5.header()->isDylib();
    bool isDylib6  = mock6.header()->isDylib();
    bool isExec1   = mock1.header()->isMainExecutable();
    bool isExec2   = mock2.header()->isMainExecutable();
    bool isExec3   = mock3.header()->isMainExecutable();
    bool isExec4   = mock4.header()->isMainExecutable();
    bool isExec5   = mock5.header()->isMainExecutable();
    bool isExec6   = mock6.header()->isMainExecutable();
    bool isBundle1 = mock1.header()->isBundle();
    bool isBundle2 = mock2.header()->isBundle();
    bool isBundle3 = mock3.header()->isBundle();
    bool isBundle4 = mock4.header()->isBundle();
    bool isBundle5 = mock5.header()->isBundle();
    bool isBundle6 = mock6.header()->isBundle();
    bool isKextBundle = mock5.header()->isKextBundle();
    bool isFileSet    = mock6.header()->isFileSet();

    // Assert: mock1 is exectuable, mock2 is dylib, and mock3 is bundle
    XCTAssertFalse(isDylib1);
    XCTAssertTrue( isDylib2);
    XCTAssertFalse(isDylib3);
    XCTAssertFalse(isDylib4);
    XCTAssertFalse(isDylib5);
    XCTAssertFalse(isDylib6);
    XCTAssertTrue( isExec1);
    XCTAssertFalse(isExec2);
    XCTAssertFalse(isExec3);
    XCTAssertFalse(isExec4);
    XCTAssertFalse(isExec5);
    XCTAssertFalse(isExec6);
    XCTAssertFalse(isBundle1);
    XCTAssertFalse(isBundle2);
    XCTAssertTrue( isBundle3);
    XCTAssertFalse(isBundle4);
    XCTAssertFalse(isBundle5);
    XCTAssertFalse(isBundle6);
    XCTAssertTrue( isKextBundle);
    XCTAssertTrue( isFileSet);
}

- (void)test_isZippered
{
    // Arrange: make mach-o files with different LC_BUILD_VERSION load commands
    MockO mockZippered(MH_DYLIB, "x86_64", Platform::macOS,  "10.15", "10.15");
    mockZippered.customizeMakeZippered();
    MockO mockMac(MH_DYLIB, "x86_64", Platform::macOS,  "10.15", "10.15");

    // Act: test isDylib()
    bool isZip = mockZippered.header()->isZippered();
    bool isMac = mockMac.header()->isZippered();

    // Assert: only mockZippered is zippered
    XCTAssertTrue(isZip);
    XCTAssertFalse(isMac);
}

- (void)test_is64
{
    // Arrange: make 32-bit and 64-bit mach-o dylibs
    MockO mockA(MH_DYLIB, "x86_64");
    MockO mockB(MH_DYLIB, "armv7k");

    // Act: gather sizes
    bool     isA64    = mockA.header()->is64();
    bool     isB64    = mockB.header()->is64();
    size_t   mhSizeA  = mockA.header()->machHeaderSize();
    size_t   mhSizeB  = mockB.header()->machHeaderSize();
    uint32_t ptrSizeA = mockA.header()->pointerSize();
    uint32_t ptrSizeB = mockB.header()->pointerSize();

    // Assert: sizes are correct for 32-bit and 64-bit mach-o
    XCTAssertTrue(isA64);
    XCTAssertFalse(isB64);
    XCTAssert(mhSizeA == 32);
    XCTAssert(mhSizeB == 28);
    XCTAssert(ptrSizeA == 8);
    XCTAssert(ptrSizeB == 4);
}

- (void)test_packedVersion
{
    // Arrange:
    char testStr1[32];
    char testStr2[32];
    char testStr3[32];

    // Act: test XXXX.YY.ZZ conversions
    dyld3::MachOFile::packedVersionToString(0x12345678, testStr1);
    dyld3::MachOFile::packedVersionToString(0xFFFFFFFF, testStr2);
    dyld3::MachOFile::packedVersionToString(0x000A0100, testStr3);

    // Assert: string formatted as expected
    XCTAssert(strcmp(testStr1, "4660.86.120") == 0);
    XCTAssert(strcmp(testStr2, "65535.255.255") == 0);
    XCTAssert(strcmp(testStr3, "10.1.0") == 0);
}

- (void)test_builtForPlatform
{
    // Arrange: three generations: LC_BUILD_VERSION, LC_VERSION_MIN_MACOSX, no load command
    MockO mockZippered(MH_DYLIB, "x86_64", Platform::macOS,  "10.15", "10.15");
    mockZippered.customizeMakeZippered();
    MockO mockMinVers(MH_DYLIB, "x86_64", Platform::macOS,  "10.10", "10.10");
    MockO mockNoVers( MH_DYLIB, "x86_64", Platform::macOS,  "10.6",  "10.6");

    // Act: test builtForPlatform()
    bool zipBuiltForMacOS     = mockZippered.header()->builtForPlatform(Platform::macOS);
    bool zipBuiltForCatalyst  = mockZippered.header()->builtForPlatform(Platform::iOSMac);
    bool zipBuiltForiOS       = mockZippered.header()->builtForPlatform(Platform::iOS);
    bool zipBuiltForOnlyMacOS = mockZippered.header()->builtForPlatform(Platform::macOS, true);
    bool minVersForMacOS      = mockMinVers.header()->builtForPlatform(Platform::macOS);
    bool minVersForiOS        = mockMinVers.header()->builtForPlatform(Platform::iOS);
    bool noLoadForMacOS       = mockNoVers.header()->builtForPlatform(Platform::macOS);
    bool noLoadForiOS         = mockNoVers.header()->builtForPlatform(Platform::iOS);

    // Assert: string formatted as expected
    XCTAssertTrue( zipBuiltForMacOS);
    XCTAssertTrue( zipBuiltForCatalyst);
    XCTAssertFalse(zipBuiltForiOS);
    XCTAssertFalse(zipBuiltForOnlyMacOS);
    XCTAssertTrue( minVersForMacOS);
    XCTAssertFalse(minVersForiOS);
    XCTAssertTrue( noLoadForMacOS);
    XCTAssertFalse(noLoadForiOS);
}


- (void)test_isMachO
{
    // Arrange: make mach-o files
    MockO  mock32(MH_EXECUTE, "armv7k");
    MockO  mock64(MH_EXECUTE, "arm64");

    // Act: test isMachO()
    Diagnostics diag32;
    bool is32 = mock32.header()->isMachO(diag32, 0x4000);
    Diagnostics diag64;
    bool is64 = mock64.header()->isMachO(diag64, 0x4000);

    // Assert: both are valid mach-o
    XCTAssertTrue(is32);
    XCTAssertTrue(is64);
    XCTAssertFalse(diag32.hasError());
    XCTAssertFalse(diag64.hasError());
}

- (void)test_isMachO_badMagic
{
    // Arrange: make mach-o file but stomp on magic field
    MockO                mock(MH_EXECUTE, "arm64");
    const MachOAnalyzer* ma = mock.header();
    const_cast<MachOAnalyzer*>(ma)->magic = 0x77777777;

    // Act: test isMachO()
    Diagnostics diag;
    bool is = ma->isMachO(diag, 0x4000);

    // Assert: isMachO() failed and returns error string with includes MH_MAGIC
    XCTAssertFalse(is);
    XCTAssertTrue(diag.hasError());
    XCTAssert(diag.errorMessageContains("MH_MAGIC"));
}

- (void)test_isMachO_badSize
{
    // Arrange: make mach-o file but truncate
    MockO mock(MH_DYLIB, "arm64");

    // Act: test isMachO()
    Diagnostics diag;
    bool is = mock.header()->isMachO(diag, 40);

    // Assert: isMachO() failed and returns error string with includes "length"
    XCTAssertFalse(is);
    XCTAssertTrue(diag.hasError());
    XCTAssert(diag.errorMessageContains("length"));
}


- (void)test_forEachLoadCommand
{
    // Arrange: make mach-o file but truncate
    MockO mock(MH_DYLIB, "arm64");

    // Act: test forEachLoadCommand()
    __block bool foundInstallname = false;
    Diagnostics  diag;
    mock.header()->forEachLoadCommand(diag, ^(const load_command* cmd, bool& stop) {
        if ( cmd->cmd == LC_ID_DYLIB )
            foundInstallname = true;
    });

    // Assert: forEachLoadCommand() looped and had no error
    XCTAssertTrue(foundInstallname);
    XCTAssertFalse(diag.hasError());
}


- (void)test_forEachLoadCommand_loadCommandSizeTooSmall
{
    // Arrange: make mach-o file but tweak load command size to be too small
    MockO mock(MH_DYLIB, "arm64");
    load_command* lc = mock.wrenchFindLoadCommand(LC_UUID);
    lc->cmdsize = 6;  // min is 8

    // Act: test forEachLoadCommand()
    __block bool foundUuid = false;
    Diagnostics  diag;
    mock.header()->forEachLoadCommand(diag, ^(const load_command* cmd, bool& stop) {
        if ( cmd->cmd == LC_UUID )
            foundUuid = true;
    });

    // Assert: forEachLoadCommand() looped and had no error
    XCTAssertFalse(foundUuid);
    XCTAssertTrue(diag.hasError());
    XCTAssert(diag.errorMessageContains("load command"));
}

- (void)test_forEachLoadCommand_loadCommandSizeTooLarge
{
    // Arrange: make mach-o file but tweak load command size to be too large
    MockO mock(MH_DYLIB, "arm64");
    load_command* lc = mock.wrenchFindLoadCommand(LC_UUID);
    lc->cmdsize = 1600; // goes beyond sizeofcmds

    // Act: test forEachLoadCommand()
    __block bool foundUuid = false;
    Diagnostics  diag;
    mock.header()->forEachLoadCommand(diag, ^(const load_command* cmd, bool& stop) {
        if ( cmd->cmd == LC_UUID )
            foundUuid = true;
    });

    // Assert: forEachLoadCommand() looped and had no error
    XCTAssertFalse(foundUuid);
    XCTAssertTrue(diag.hasError());
    XCTAssert(diag.errorMessageContains("load command"));
}

- (void)test_getUuid
{
    // Arrange: make mach-o files: one without a uuid, and one with a random uuid
    MockO mock1(MH_EXECUTE, "arm64");
    MockO mock2(MH_EXECUTE, "arm64");
    mock2.wrenchRemoveUUID();

    // Act: test getUuid()
    uuid_t uuid1;
    uuid_t uuid2;
    bool hasUuid1 = mock1.header()->getUuid(uuid1);
    bool hasUuid2 = mock2.header()->getUuid(uuid2);

    // Assert: only mock2 has uuid, and its returned as same value as "aUUID"
    XCTAssertTrue(hasUuid1);
    XCTAssertFalse(hasUuid2);
}

- (void)test_getDylibInstallName
{
    // Arrange: make three mach-o files: one non-dylib, one dylib without LC_ID_DYLIB, and one with
    MockO mock1(MH_EXECUTE, "arm64");
    MockO mock2(MH_DYLIB, "arm64");
    MockO mock3(MH_DYLIB, "arm64");
    mock3.customizeInstallName("/foo/bar/libtest.dylib", 11, 42);

    // Act: test getDylibInstallName()
    const char* installName1;
    const char* installName2;
    const char* installName3;
    uint32_t    currentVersion1;
    uint32_t    currentVersion2;
    uint32_t    currentVersion3;
    uint32_t    compatVersion1;
    uint32_t    compatVersion2;
    uint32_t    compatVersion3;
    bool        hasInstallName1 = mock1.header()->getDylibInstallName(&installName1, &compatVersion1, &currentVersion1);
    bool        hasInstallName2 = mock2.header()->getDylibInstallName(&installName2, &compatVersion2, &currentVersion2);
    bool        hasInstallName3 = mock3.header()->getDylibInstallName(&installName3, &compatVersion3, &currentVersion3);

    // Assert: only mock2 has installName, and its returned values are correct
    XCTAssertFalse(hasInstallName1);
    XCTAssertTrue(hasInstallName2);
    XCTAssertTrue(hasInstallName3);
    XCTAssert(strcmp(installName3, "/foo/bar/libtest.dylib") == 0);
    XCTAssert(compatVersion3 == 11);
    XCTAssert(currentVersion3 == 42);
}

- (void)test_DyldEnv
{
   // Arrange: make mach-o with two LC_DYLD_ENVIRONMENT
    MockO mock(MH_EXECUTE, "arm64");
    mock.customizeAddDyldEnvVar("DYLD_FRAMEWORK_PATH=/foo/bar");
    mock.customizeAddDyldEnvVar("DYLD_LIBRARY_PATH=/opt/lib");

    // Act: test forDyldEnv() returns both embedded env vars
    __block bool foundFramework = false;
    __block bool foundLibary    = false;
    __block bool foundOther     = false;
    mock.header()->forDyldEnv(^(const char* envVar, bool& stop) {
        if ( strcmp(envVar, "DYLD_FRAMEWORK_PATH=/foo/bar") == 0 )
            foundFramework = true;
        else if ( strcmp(envVar, "DYLD_LIBRARY_PATH=/opt/lib") == 0 )
            foundLibary = true;
        else
            foundOther = true;
    });

    // Assert: just the two env vars were found
    XCTAssertTrue(foundFramework);
    XCTAssertTrue(foundLibary);
    XCTAssertFalse(foundOther);
}

- (void)test_DyldEnvIgnore
{
   // Arrange: make mach-o with unsupported LC_DYLD_ENVIRONMENT
    MockO mock(MH_EXECUTE, "arm64");
    mock.customizeAddDyldEnvVar("DYLD_OTHER_STUFF=1");
    mock.customizeAddDyldEnvVar("CORE_LIBRARY_PATH=/opt/lib");

    // Act: test forDyldEnv() returns no env vars
    __block bool foundEnvVar = false;
    mock.header()->forDyldEnv(^(const char* envVar, bool& stop) {
        foundEnvVar = true;
    });

    // Assert: no env vars returned (none match DYLD_*_PATH pattern)
    XCTAssertFalse(foundEnvVar);
}

- (void)test_forEachRPath
{
   // Arrange: make mach-o with two LC_RPATH
    MockO mock(MH_EXECUTE, "arm64");
    mock.customizeAddRPath("@loader_path/..");
    mock.customizeAddRPath("/usr/lib");

    // Act: test forEachRPath() iterates both embedded rpaths
    __block int  index = 0;
    const char*  rpathsArray[3];
    const char** rpaths = rpathsArray; // compiler work around
    mock.header()->forEachRPath(^(const char* rPath, bool& stop) {
        if ( index < 2 )
            rpaths[index] = rPath;
        ++index;
    });

    // Assert: just the two rapths were found
    XCTAssert(index == 2);
    XCTAssert(strcmp(rpaths[0], "@loader_path/..") == 0);
    XCTAssert(strcmp(rpaths[1], "/usr/lib") == 0);
}

- (void)test_forEachDependentDylib
{
   // Arrange: make mach-o with four dependents
    MockO mock(MH_DYLIB, "arm64");
    mock.customizeAddDependentDylib("/usr/lib/libalways.dylib");
    mock.customizeAddDependentDylib("/usr/lib/libweak.dylib", true);
    mock.customizeAddDependentDylib("/usr/lib/libupward.dylib", false, true);
    mock.customizeAddDependentDylib("/usr/lib/libreexport.dylib", false, false, true);

    // Act: test forEachDependent()
    struct Info { const char* loadPath; bool isWeak; bool isReExport; bool isUpward; uint32_t compatVers; uint32_t curVers; };
    __block int  index = 0;
    Info         infoArray[6];
    Info*        infos = infoArray; // compiler work around
    mock.header()->forEachDependentDylib(^(const char* loadPath, bool isWeak, bool isReExport, bool isUpward, uint32_t compatVers, uint32_t curVers, bool& stop) {
        if ( index < 5 ) {
            infos[index].loadPath   = loadPath;
            infos[index].isWeak     = isWeak;
            infos[index].isReExport = isReExport;
            infos[index].isUpward   = isUpward;
            infos[index].compatVers = compatVers;
            infos[index].curVers    = curVers;
        }
        ++index;
    });

    // Assert: all dependent info correct
    XCTAssert(index == 5);
    XCTAssert(strcmp(infos[0].loadPath, "/usr/lib/libSystem.B.dylib") == 0);
    XCTAssertFalse(infos[0].isWeak);
    XCTAssertFalse(infos[0].isReExport);
    XCTAssertFalse(infos[0].isUpward);
    XCTAssert(strcmp(infos[1].loadPath, "/usr/lib/libalways.dylib") == 0);
    XCTAssertFalse(infos[1].isWeak);
    XCTAssertFalse(infos[1].isReExport);
    XCTAssertFalse(infos[1].isUpward);
    XCTAssert(strcmp(infos[2].loadPath, "/usr/lib/libweak.dylib") == 0);
    XCTAssertTrue( infos[2].isWeak);
    XCTAssertFalse(infos[2].isReExport);
    XCTAssertFalse(infos[2].isUpward);
    XCTAssert(strcmp(infos[3].loadPath, "/usr/lib/libupward.dylib") == 0);
    XCTAssertFalse(infos[3].isWeak);
    XCTAssertFalse(infos[3].isReExport);
    XCTAssertTrue( infos[3].isUpward);
    XCTAssert(strcmp(infos[4].loadPath, "/usr/lib/libreexport.dylib") == 0);
    XCTAssertFalse(infos[4].isWeak);
    XCTAssertTrue( infos[4].isReExport);
    XCTAssertFalse(infos[4].isUpward);
}

- (void)test_forEachDependentDylib_minimal
{
   // Arrange: make mach-o with one implicit dependent
    MockO mock(MH_EXECUTE, "x86_64", Platform::macOS, "10.15", "10.15");

    // Act: test that forEachDependent() synthesizes a link to libSystem.B.dylib
    struct Info { const char* loadPath; bool isWeak; bool isReExport; bool isUpward; uint32_t compatVers; uint32_t curVers; };
    __block int  index = 0;
    Info         infoArray[5];
    Info*        infos = infoArray; // compiler work around
    mock.header()->forEachDependentDylib(^(const char* loadPath, bool isWeak, bool isReExport, bool isUpward, uint32_t compatVers, uint32_t curVers, bool& stop) {
        if ( index < 4 ) {
            infos[index].loadPath   = loadPath;
            infos[index].isWeak     = isWeak;
            infos[index].isReExport = isReExport;
            infos[index].isUpward   = isUpward;
            infos[index].compatVers = compatVers;
            infos[index].curVers    = curVers;
        }
        ++index;
    });

    // Assert: libSystem.dylib was only dependent
    XCTAssert(index == 1);
    XCTAssert(strcmp(infos[0].loadPath, "/usr/lib/libSystem.B.dylib") == 0);
    XCTAssertFalse(infos[0].isWeak);
    XCTAssertFalse(infos[0].isReExport);
    XCTAssertFalse(infos[0].isUpward);
}


- (void)test_RestrictSegment
{
   // Arrange: make mach-o with and without __RESTRICT/__restrict section
    MockO mock1(MH_EXECUTE, "arm64");
    MockO mock2(MH_EXECUTE, "arm64");
    mock2.customizeAddSegment("__RESTRICT");
    mock2.customizeAddSection("__RESTRICT", "__restrict");

    // Act: test isRestricted()
    bool restricted1 = mock1.header()->isRestricted();
    bool restricted2 = mock2.header()->isRestricted();

    // Assert: only mock2 is resticted
    XCTAssertFalse(restricted1);
    XCTAssertTrue(restricted2);
}

- (void)test_AltPlatformSection
{
   // Arrange: make mach-o with and without __RESTRICT/__restrict section
    MockO mock1(MH_EXECUTE, "arm64");
    MockO mock2(MH_EXECUTE, "arm64");
    mock2.customizeAddSection("__DATA", "__allow_alt_plat");

    // Act: test isRestricted()
    bool alt1 = mock1.header()->allowsAlternatePlatform();
    bool alt2 = mock2.header()->allowsAlternatePlatform();

    // Assert: only mock2 is resticted
    XCTAssertFalse(alt1);
    XCTAssertTrue(alt2);
}

- (void)test_AltPlatformSection32
{
   // Arrange: make mach-o with and without __RESTRICT/__restrict section
    MockO mock1(MH_EXECUTE, "arm64_32");
    MockO mock2(MH_EXECUTE, "arm64_32");
    mock2.customizeAddSection("__DATA", "__allow_alt_plat");

    // Act: test isRestricted()
    bool alt1 = mock1.header()->allowsAlternatePlatform();
    bool alt2 = mock2.header()->allowsAlternatePlatform();

    // Assert: only mock2 is resticted
    XCTAssertFalse(alt1);
    XCTAssertTrue(alt2);
}

- (void)test_hasInitializers64
{
   // Arrange: make mach-o with all the different forms of initializers
    MockO mockNoInits(MH_EXECUTE, "x86_64");
    MockO mockModInit(MH_EXECUTE, "x86_64", Platform::macOS, "10.10");
    mockModInit.customizeAddInitializer();
    MockO mockInitOffsets(MH_EXECUTE, "x86_64", Platform::macOS, "12.0");
    mockInitOffsets.customizeAddInitializer();

    // Act: test hasInitializer()
    Diagnostics diag;
    bool hasInit1 = mockNoInits.header()->hasInitializer(diag);
    bool hasInit2 = mockModInit.header()->hasInitializer(diag);
    bool hasInit3 = mockInitOffsets.header()->hasInitializer(diag);

    // Assert: only mockModInit and mockInitOffsets have initializers
    XCTAssertFalse(hasInit1);
    XCTAssertTrue(hasInit2);
    XCTAssertTrue(hasInit3);
}

- (void)test_hasInitializers32
{
   // Arrange: make mach-o with all the different forms of initializers
    MockO mockNoInits(MH_EXECUTE, "arm64_32");
    MockO mockModInit(MH_EXECUTE, "arm64_32", Platform::watchOS, "6.0");
    mockModInit.customizeAddInitializer();
    MockO mockInitOffsets(MH_EXECUTE, "arm64_32", Platform::watchOS, "8.0");
    mockInitOffsets.customizeAddInitializer();

    // Act: test hasInitializer()
    Diagnostics diag;
    bool hasInit1 = mockNoInits.header()->hasInitializer(diag);
    bool hasInit2 = mockModInit.header()->hasInitializer(diag);
    bool hasInit3 = mockInitOffsets.header()->hasInitializer(diag);

    // Assert: only mockModInit and mockInitOffsets have initializers
    XCTAssertFalse(hasInit1);
    XCTAssertTrue(hasInit2);
    XCTAssertTrue(hasInit3);
}

- (void)test_validMainExecutable
{
    // Arrange: make mach-o files
    // regular arm64 macOS binary
    MockO mockGood(MH_EXECUTE, "arm64");
    // static x86_64 macOS binary
    MockO mockNoDyld(MH_EXECUTE, "x86_64");
    mockNoDyld.wrenchRemoveDyld();
    mockNoDyld.wrenchSetNoDependentDylibs();
    // regular arm64 iOS binary
    MockO mockPlatform(MH_EXECUTE, "arm64", Platform::iOS, "14.0");
    // regular arm64 macOS binary that links with nothing
    MockO mockNoLib(MH_EXECUTE, "arm64");
    mockNoLib.wrenchSetNoDependentDylibs();

    // Act: test isValidMainExecutable()
    const char* execPath = "/foo/exec";
    Diagnostics diagGood;
    bool isValidMockGood = mockGood.header()->isValidMainExecutable(diagGood, execPath, mockGood.size(), GradedArchs::arm64, Platform::macOS);
    Diagnostics diagNoDyld;
    bool isMockNoDyldValidMain = mockNoDyld.header()->isValidMainExecutable(diagNoDyld, execPath, mockNoDyld.size(), GradedArchs::x86_64, Platform::macOS);
    Diagnostics diagPlatform;
    bool isMockPlatformValidMain = mockPlatform.header()->isValidMainExecutable(diagPlatform, execPath, mockPlatform.size(), GradedArchs::arm64, Platform::iOS);
    bool isMockPlatformValidMainB = mockPlatform.header()->isValidMainExecutable(diagPlatform, execPath, mockPlatform.size(), GradedArchs::arm64, Platform::tvOS);
    Diagnostics diagNoLib;
    bool isMockNoLibValidMain = mockNoLib.header()->isValidMainExecutable(diagNoLib, execPath, mockNoLib.size(), GradedArchs::arm64, Platform::macOS);

    // Assert: mock is a valid main executable
    XCTAssertTrue(isValidMockGood);
    XCTAssertFalse(isMockNoDyldValidMain);
    XCTAssertTrue(diagNoDyld.errorMessageContains("static executable"));
    XCTAssertTrue(isMockPlatformValidMain);
    XCTAssertFalse(isMockPlatformValidMainB);
    XCTAssertTrue(diagPlatform.errorMessageContains("was not built for platform"));
    XCTAssertFalse(isMockNoLibValidMain);
    XCTAssertTrue(diagNoLib.errorMessageContains("missing LC_LOAD_DYLIB"));
}

- (void)test_validMainExecutable_driverkit
{
    // Arrange: make mach-o files
    MockO mockDriverKitGood(MH_EXECUTE, "x86_64", Platform::driverKit, "21.0");
    MockO mockDriverKitWithMain(MH_EXECUTE, "x86_64", Platform::driverKit, "21.0");
    mockDriverKitWithMain.wrenchAddMain();

    // Act: test isValidMainExecutable()
    const char* execPath = "/foo/exec";
    Diagnostics diagGood;
    bool isMockDriverKitGood = mockDriverKitGood.header()->isValidMainExecutable(diagGood, execPath, mockDriverKitGood.size(), GradedArchs::x86_64, Platform::driverKit);
    Diagnostics diagWithMain;
    bool isMockDriverKitWithMain = mockDriverKitWithMain.header()->isValidMainExecutable(diagWithMain, execPath, mockDriverKitWithMain.size(), GradedArchs::x86_64, Platform::driverKit);

    // Assert: mock is a valid main executable
    XCTAssertTrue(isMockDriverKitGood);
    XCTAssertFalse(isMockDriverKitWithMain);
    XCTAssertTrue(diagWithMain.errorMessageContains("LC_MAIN not allowed for driverkit"));
}

// test that newer main executables cannot have cache bit set
- (void)test_validMainExecutable_cache_bit
{
    // Arrange: make mach-o files
    // regular x86_64 macOS binary that has in-dyld-cache bit set
    MockO mockInCacheNew(MH_EXECUTE, "x86_64");
    ((mach_header*)mockInCacheNew.header())->flags |= 0x80000000;
    // regular x86_64 macOS binary that has in-dyld-cache bit set but for 10.15
    MockO mockInCacheOld(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    ((mach_header*)mockInCacheOld.header())->flags |= 0x80000000;


    // Act: call isValidMainExecutable()
    const char* execPath = "/foo/exec";
    Diagnostics diag_mockInCacheNew;
    bool isValid_mockInCacheNew = mockInCacheNew.header()->isValidMainExecutable(diag_mockInCacheNew, execPath, mockInCacheNew.size(), GradedArchs::x86_64, Platform::macOS);
    Diagnostics diag_mockInCacheOld;
    bool isValid_mockInCacheOld = mockInCacheOld.header()->isValidMainExecutable(diag_mockInCacheOld, execPath, mockInCacheOld.size(), GradedArchs::x86_64, Platform::macOS);


    // Assert: mock is a valid main executable
    XCTAssertTrue(isValid_mockInCacheOld);
    XCTAssertFalse(isValid_mockInCacheNew);
    XCTAssertTrue(diag_mockInCacheNew.errorMessageContains("MH_EXECUTE is in dyld shared cache"));
}


- (void)test_forEachSupportedPlatform_oldMacOS
{
   // Arrange: make mach-o with a range of OS version info
    MockO mock10_5(MH_EXECUTE, "x86_64", Platform::macOS, "10.5", "0.0"); // old binary with no sdk
    MockO mock10_6(MH_EXECUTE, "x86_64", Platform::macOS, "10.6", "0.0");  // old binary with no sdk
    MockO mock10_6sdk(MH_EXECUTE, "x86_64", Platform::macOS, "10.6", "10.8");
    MockO mock10_7(MH_EXECUTE, "x86_64", Platform::macOS, "10.7", "10.8");

    // Act: check parsing of platform info
    __block Platform plat10_5  = Platform::unknown;
    __block uint32_t minOS10_5 = 0xFFFFFFFF;
    __block uint32_t sdk10_5   = 0xFFFFFFFF;
    mock10_5.header()->forEachSupportedPlatform(^(Platform platform, uint32_t minOS, uint32_t sdk) {
        plat10_5  = platform;
        minOS10_5 = minOS;
        sdk10_5   = sdk;
    });
    __block Platform plat10_6  = Platform::unknown;
    __block uint32_t minOS10_6 = 0xFFFFFFFF;
    __block uint32_t sdk10_6   = 0xFFFFFFFF;
    mock10_6.header()->forEachSupportedPlatform(^(Platform platform, uint32_t minOS, uint32_t sdk) {
        plat10_6  = platform;
        minOS10_6 = minOS;
        sdk10_6   = sdk;
    });
    __block Platform plat10_6sdk  = Platform::unknown;
    __block uint32_t minOS10_6sdk = 0xFFFFFFFF;
    __block uint32_t sdk10_6sdk   = 0xFFFFFFFF;
    mock10_6sdk.header()->forEachSupportedPlatform(^(Platform platform, uint32_t minOS, uint32_t sdk) {
        plat10_6sdk  = platform;
        minOS10_6sdk = minOS;
        sdk10_6sdk   = sdk;
    });
    __block Platform plat10_7  = Platform::unknown;
    __block uint32_t minOS10_7 = 0xFFFFFFFF;
    __block uint32_t sdk10_7   = 0xFFFFFFFF;
    mock10_7.header()->forEachSupportedPlatform(^(Platform platform, uint32_t minOS, uint32_t sdk) {
        plat10_7  = platform;
        minOS10_7 = minOS;
        sdk10_7   = sdk;
    });

    // Assert: old macOS version info parsed properly
    XCTAssert(plat10_5  == Platform::macOS);
    XCTAssert(minOS10_5 == 0x0000A0500);
    XCTAssert(sdk10_5   == 0x0000A0500);    // note: load command has no SDK info, so MachOFile should use minOS for SDK
    XCTAssert(plat10_6  == Platform::macOS);
    XCTAssert(minOS10_6 == 0x0000A0600);
    XCTAssert(sdk10_6   == 0x0000A0600);    // note: load command has no SDK info, so MachOFile should use minOS for SDK
    XCTAssert(plat10_6sdk  == Platform::macOS);
    XCTAssert(minOS10_6sdk == 0x0000A0600);
    XCTAssert(sdk10_6sdk   == 0x0000A0800);
    XCTAssert(plat10_7  == Platform::macOS);
    XCTAssert(minOS10_7 == 0x0000A0700);
    XCTAssert(sdk10_7   == 0x0000A0800);    // note: load command has no SDK info, so MachOFile should use minOS for SDK
}


- (void)test_fat_archnames
{
    // Arrange: make FAT file with a range of slices
    MockO mock1(MH_EXECUTE, "x86_64");
    MockO mock2(MH_EXECUTE, "arm64e");
    Muckle muckle;
    muckle.addMockO(&mock1);
    muckle.addMockO(&mock2);

    // Act: check parsing of platform info
    char strBuf[256] = { '\0' };
    muckle.header()->archNames(strBuf);

    // Assert: arch names is as expected
    XCTAssert(!strcmp(strBuf, "x86_64,arm64e"));
}


- (void)test_fat_archnames_mismatched_macho
{
    // Arrange: make FAT file with a range of slices
    MockO mock1(MH_EXECUTE, "x86_64");
    MockO mock2(MH_EXECUTE, "arm64e");
    Muckle muckle;
    muckle.addMockO(&mock1);
    muckle.addMockO(&mock2);

    // Change the arch on one of the slices
    Diagnostics sliceDiag;
    muckle.header()->forEachSlice(sliceDiag, 0xFFFFFFFF, ^(uint32_t sliceCpuType, uint32_t sliceCpuSubType, const void *sliceStart, uint64_t sliceSize, bool &stop) {
        MachOAnalyzer* ma = (MachOAnalyzer*)sliceStart;
        if ( ma->isArch("arm64e") )
            ma->cpusubtype = 0;
    });

    // Act: check parsing of platform info
    char strBuf[256] = { '\0' };
    muckle.header()->archNames(strBuf);

    // Assert: arch names is as expected
    XCTAssert(!strcmp(strBuf, "x86_64,arm64e"));
}

@end
