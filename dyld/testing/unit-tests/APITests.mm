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
using dyld4::SyscallDelegate;
using dyld3::Platform;

//
// This tests methods in the APIs class.  Thus, by calling tester.apis.dlopen()
// it is calling the dlopen linked into the unit test, and not the OS dlopen().
//
class TestState
{
public:
                         TestState(SyscallDelegate& sys, MockO& main, const std::vector<const char*>& envp={},
                                   const std::vector<const char*>& apple={"executable_path=/foo/test.exe"});
                         TestState(SyscallDelegate& sys, const std::vector<const char*>& envp, const std::vector<const char*>& apple);
                         TestState(MockO& main, const std::vector<const char*>& envp={},
                                   const std::vector<const char*>& apple={"executable_path=/foo/test.exe"});
                         TestState(const std::vector<const char*>& envp={});
    SyscallDelegate&     osDelegate()   { return _osDelegate; }

private:
    MockO                _defaultMain;
    SyscallDelegate      _osDelegate;
    KernelArgs           _kernArgs;
    ProcessConfig        _config;
public:
    APIs&                apis;
};

TestState::TestState(SyscallDelegate& sys, MockO& main, const std::vector<const char*>& envp, const std::vector<const char*>& apple)
    : _defaultMain(MH_EXECUTE, "arm64"), _osDelegate(sys), _kernArgs(main.header(), {"test.exe"}, envp, apple),
      _config(&_kernArgs, _osDelegate), apis(APIs::bootstrap(_config))
{
}

TestState::TestState(SyscallDelegate& sys, const std::vector<const char*>& envp, const std::vector<const char*>& apple)
    : _defaultMain(MH_EXECUTE, "arm64"), _kernArgs(_defaultMain.header(), {"test.exe"}, envp, apple),
      _config(&_kernArgs, _osDelegate), apis(APIs::bootstrap(_config))
{
}

TestState::TestState(MockO& main, const std::vector<const char*>& envp, const std::vector<const char*>& apple)
    :  _defaultMain(MH_EXECUTE, "arm64"), _kernArgs(main.header(), {"test.exe"}, envp, apple),
      _config(&_kernArgs, _osDelegate), apis(APIs::bootstrap(_config))
{
}

TestState::TestState(const std::vector<const char*>& envp)
    :  _defaultMain(MH_EXECUTE, "arm64"), _kernArgs(_defaultMain.header(), {"test.exe"}, envp, {"executable_path=/foo/test.exe"}),
      _config(&_kernArgs, _osDelegate), apis(APIs::bootstrap(_config))
{
}

@interface APITests : XCTestCase
@end

@implementation APITests

- (void)testEmpty_dyld_image_count
{
    // Arrange: set up RuntimeState
    TestState tester;

    // Act:
    uint32_t count = tester.apis._dyld_image_count();

    // Assert:
    XCTAssert(count == 0);
}


- (void)testDefault_dyld_has_inserted_or_interposing_libraries
{
   // Arrange: set up default RuntimeState
    TestState tester;

    // Act: call dyld_has_inserted_or_interposing_libraries()
    bool hasInsertsOrInterposes = tester.apis.dyld_has_inserted_or_interposing_libraries();

    // Assert: dyld_has_inserted_or_interposing_libraries() should have returned false
    XCTAssertFalse(hasInsertsOrInterposes);
}

- (void)testInserted_dyld_has_inserted_or_interposing_libraries
{
   // Arrange: set up RuntimeState with DYLD_INSERT_LIBRARIES
    TestState tester({"DYLD_INSERT_LIBRARIES=/tmp/foo"});

    // Act: call dyld_has_inserted_or_interposing_libraries()
    bool hasInsertsOrInterposes = tester.apis.dyld_has_inserted_or_interposing_libraries();

    // Assert: dyld_has_inserted_or_interposing_libraries() should have returned true
    XCTAssertTrue(hasInsertsOrInterposes);
}

- (void)test_iOS_legacy_version_SPIs {
    // Arrange: make binary built with iOS 13.0 (Fall 2019) SDK
    MockO main(MH_EXECUTE, "arm64", Platform::iOS, "12.0", "13.0");
    TestState tester(main);

    // Act: call dyld_get_program_{min_os|sdk}_version()
    uint32_t sdkVersion     = tester.apis.dyld_get_program_sdk_version();
    uint32_t minOSVersion   = tester.apis.dyld_get_program_min_os_version();

    // Assert: The correct versions are returned
    XCTAssertEqual(sdkVersion,      0x000D0000);
    XCTAssertEqual(minOSVersion,    0x000C0000);
}

- (void)test_macOS_legacy_version_SPIs {
    // Arrange: make binary built with macOS 11.0 (Fall 2020) SDK
    MockO main(MH_EXECUTE, "arm64", Platform::macOS, "10.12", "11.0");
    TestState tester(main);

    // Act: call dyld_get_program_{min_os|sdk}_version()
    uint32_t sdkVersion     = tester.apis.dyld_get_program_sdk_version();
    uint32_t minOSVersion   = tester.apis.dyld_get_program_min_os_version();

    // Assert: The correct versions are returned
    XCTAssertEqual(sdkVersion,      0x000B0000);
    XCTAssertEqual(minOSVersion,    0x000A0C00);
}

- (void)test_iOSMac_legacy_version_SPIs {
    // Arrange: make binary built with macOS 11.0 (Fall 2020) SDK
    MockO main(MH_EXECUTE, "arm64", Platform::iOSMac, "12.0", "13.0");
    TestState tester(main);

    // Act: call dyld_get_program_{min_os|sdk}_version()
    uint32_t sdkVersion     = tester.apis.dyld_get_program_sdk_version();
    uint32_t minOSVersion   = tester.apis.dyld_get_program_min_os_version();

    // Assert: The correct versions are returned
    XCTAssertEqual(sdkVersion,      0x000D0000);
    XCTAssertEqual(minOSVersion,    0x000C0000);
}

- (void)test_watchOS_legacy_version_SPIs {
    // Arrange: make binary built with watchOS 6.0 (Fall 2019) SDK
    MockO main(MH_EXECUTE, "arm64_32", Platform::watchOS, "5.0", "6.0");
    TestState tester(main);

    // Act: call dyld_get_program_{min_os|sdk}{_watch_os}_version()
    uint32_t mangledSdkVersion      = tester.apis.dyld_get_program_sdk_version();
    uint32_t mangledMinOSVersion    = tester.apis.dyld_get_program_min_os_version();
    uint32_t sdkVersion             = tester.apis.dyld_get_program_sdk_watch_os_version();
    uint32_t minOSVersion           = tester.apis.dyld_get_program_min_watch_os_version();

    // Assert: The correct versions are returned
    XCTAssertEqual(mangledSdkVersion,   0x000D0000);
    XCTAssertEqual(mangledMinOSVersion, 0x000C0000);
    XCTAssertEqual(sdkVersion,          0x00060000);
    XCTAssertEqual(minOSVersion,        0x00050000);
}

- (void)test_bridgeOS_legacy_version_SPIs {
    // Arrange: make binary built with bridgeOS 4.0 (Fall 2019) SDK
    MockO main(MH_EXECUTE, "arm64_32", Platform::bridgeOS, "3.0", "4.0");
    TestState tester(main);

    // Act: call dyld_get_program_{min_os|sdk}{_bridge_os}_version()
    uint32_t mangledSdkVersion      = tester.apis.dyld_get_program_sdk_version();
    uint32_t mangledMinOSVersion    = tester.apis.dyld_get_program_min_os_version();
    uint32_t sdkVersion             = tester.apis.dyld_get_program_sdk_bridge_os_version();
    uint32_t minOSVersion           = tester.apis.dyld_get_program_min_bridge_os_version();

    // Assert: The correct versions are returned
    XCTAssertEqual(mangledSdkVersion,   0x000D0000);
    XCTAssertEqual(mangledMinOSVersion, 0x000C0000);
    XCTAssertEqual(sdkVersion,          0x00040000);
    XCTAssertEqual(minOSVersion,        0x00030000);
}

- (void)testEpoch_iOS_dyld_sdk_at_least
{
    // Arrange: make binary built with iOS 13.0 (Fall 2019) SDK
    MockO main(MH_EXECUTE, "arm64", Platform::iOS, "13.0", "13.0");
    TestState tester(main);

    // Act: test dyld_sdk_at_least()
    bool summer     = tester.apis.dyld_sdk_at_least(main.header(), dyld_summer_2019_os_versions);
    bool fall       = tester.apis.dyld_sdk_at_least(main.header(), dyld_fall_2019_os_versions);
    bool winter     = tester.apis.dyld_sdk_at_least(main.header(), dyld_winter_2019_os_versions);
    bool summerFast = tester.apis.dyld_program_sdk_at_least(dyld_summer_2019_os_versions);
    bool fallFast   = tester.apis.dyld_program_sdk_at_least(dyld_fall_2019_os_versions);
    bool winterFast = tester.apis.dyld_program_sdk_at_least(dyld_winter_2019_os_versions);

    // Assert: a binary built against iOS 13.0 SDK as built with at least fall 2019
    XCTAssertTrue(summer);
    XCTAssertTrue(fall);
    XCTAssertFalse(winter); // built with later iOS 13.3 sdk
    XCTAssertTrue(summerFast);
    XCTAssertTrue(fallFast);
    XCTAssertFalse(winterFast); // built with later iOS 13.3 sdk
}


- (void)testEpoch_macOS_dyld_sdk_at_least
{
    // Arrange: make binary built with macOS 10.15 (Fall 2019) SDK
    MockO main(MH_EXECUTE, "arm64", Platform::macOS, "10.15");
    TestState tester(main);

    // Act: test dyld_sdk_at_least()
    bool summer = tester.apis.dyld_sdk_at_least(main.header(), dyld_summer_2019_os_versions);
    bool fall   = tester.apis.dyld_sdk_at_least(main.header(), dyld_fall_2019_os_versions);
    bool winter = tester.apis.dyld_sdk_at_least(main.header(), dyld_winter_2019_os_versions);
    bool summerFast = tester.apis.dyld_program_sdk_at_least(dyld_summer_2019_os_versions);
    bool fallFast   = tester.apis.dyld_program_sdk_at_least(dyld_fall_2019_os_versions);
    bool winterFast = tester.apis.dyld_program_sdk_at_least(dyld_winter_2019_os_versions);

    // Assert: a binary built against macOS 10.15 SDK as built with at least fall 2019
    XCTAssertTrue(summer);
    XCTAssertTrue(fall);
    XCTAssertFalse(winter); // built with later macOS 10.15.1 SDK
    XCTAssertTrue(summerFast);
    XCTAssertTrue(fallFast);
    XCTAssertFalse(winterFast); // built with later macOS 10.15.1 SDK
}

- (void)testEpoch_macOS_nosdkVersion_dyld_sdk_at_least {
    // Arrange: make binary built for macOS 10.6, but no SDK
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.6", "0.0");
    TestState tester(main);

    // Act: test dyld_sdk_at_least()
    bool normal = tester.apis.dyld_sdk_at_least(main.header(), dyld_fall_2011_os_versions);
    bool fast = tester.apis.dyld_program_sdk_at_least(dyld_fall_2011_os_versions);

    // Assert: a binary built against macOS 10.6 SDK should fail testing against dyld_fall_2011_os_versions (10.7), the first version set
    XCTAssertFalse(normal);
    XCTAssertFalse(fast);
}

- (void)testEpoch_unknownPlatform_dyld_sdk_at_least
{
    // Arrange: make binary built with an unknown (future) platform
    MockO main(MH_EXECUTE, "arm64", (dyld3::Platform)999, "10.15", "10.15");
    TestState tester(main);

    // Act: test dyld_sdk_at_least()
    bool summer = tester.apis.dyld_sdk_at_least(main.header(), dyld_summer_2019_os_versions);
    bool fall   = tester.apis.dyld_sdk_at_least(main.header(), dyld_fall_2019_os_versions);
    bool winter = tester.apis.dyld_sdk_at_least(main.header(), dyld_winter_2019_os_versions);
    bool summerFast = tester.apis.dyld_program_sdk_at_least(dyld_summer_2019_os_versions);
    bool fallFast   = tester.apis.dyld_program_sdk_at_least(dyld_fall_2019_os_versions);
    bool winterFast = tester.apis.dyld_program_sdk_at_least(dyld_winter_2019_os_versions);

    // Assert: a binary built against an unknown SDK will be newer than any known versions
    XCTAssertFalse(summer);
    XCTAssertFalse(fall);
    XCTAssertFalse(winter); // built with later macOS 10.15.1 SDK
    XCTAssertFalse(summerFast);
    XCTAssertFalse(fallFast);
    XCTAssertFalse(winterFast); // built with later macOS 10.15.1 SDK

}

- (void)testEpoch_bogusPlatformVersion_dyld_sdk_at_least
{
    // Arrange: make binary built with macOS 11.0 (Fall 2020) SDK
    MockO main(MH_EXECUTE, "arm64", Platform::macOS, "10.12", "11.0");
    TestState tester(main);
    dyld_build_version_t bogusPlatformVersion = { .platform = 0xffff0000, .version = 1 };

    // Act: call dyld_sdk_at_least with bogus platform and version
    bool sdk = tester.apis.dyld_sdk_at_least(main.header(), bogusPlatformVersion);
    bool progSdk = tester.apis.dyld_program_sdk_at_least(bogusPlatformVersion);

    // Assert: a bogus Platform version argument returns false
    XCTAssertFalse(sdk);
    XCTAssertFalse(progSdk);
}

- (void)testEpoch_simulatorPlatformVersion_dyld_sdk_at_least
{
    // Arrange: make iOS simulator binary
    MockO main(MH_EXECUTE, "arm64", Platform::iOS_simulator, "12.0");
    TestState tester(main);
    dyld_build_version_t simPlatformVersion = { .platform = (uint32_t)dyld3::Platform::iOS_simulator, .version = 0 };

    // Act: call dyld_sdk_at_least with made up sim platform and version
    bool sdk = tester.apis.dyld_sdk_at_least(main.header(), simPlatformVersion);
    bool progSdk = tester.apis.dyld_program_sdk_at_least(simPlatformVersion);

    // Assert: a sim Platform version argument returns true
    XCTAssertTrue(sdk);
    XCTAssertTrue(progSdk);
}

- (void)testEpoch_NoBuildVersion_dyld_sdk_at_least
{
    // Arrange: make binary built with no build version
    MockO main(MH_EXECUTE, "arm64");
    main.wrenchRemoveVersionInfo();
    TestState tester(main);

    // Act: call dyld_sdk_at_least
    bool sdk = tester.apis.dyld_sdk_at_least(main.header(), dyld_fall_2019_os_versions);
    bool progSdk = tester.apis.dyld_program_sdk_at_least(dyld_fall_2019_os_versions);

    // Assert: no build version info returns false
    XCTAssertFalse(sdk);
    XCTAssertFalse(progSdk);
}

- (void)testEpoch_catalyst_dyld_sdk_at_least
{
   // Arrange: set up RuntimeState with DYLD_FORCE_PLATFORM
    MockO main(MH_EXECUTE, "arm64", Platform::macOS, "12.0");
    main.customizeAddSection("__DATA", "__allow_alt_plat");
    TestState tester(main, {"DYLD_FORCE_PLATFORM=6"});

    // Act: test dyld_{sdk|minos}_at_least()
    bool sdk = tester.apis.dyld_sdk_at_least(main.header(), dyld_platform_version_iOS_14_0);
    bool minOS = tester.apis.dyld_minos_at_least(main.header(), dyld_platform_version_iOS_14_0);
    bool progSdk = tester.apis.dyld_program_sdk_at_least(dyld_platform_version_iOS_14_0);
    bool progMinOS = tester.apis.dyld_program_minos_at_least(dyld_platform_version_iOS_14_0);

    // Assert: a binary built against macOS 12 SDK as built with at least iOS 14 under catalyst runtime
    XCTAssertTrue(sdk);
    XCTAssertTrue(minOS);
    XCTAssertTrue(progSdk);
    XCTAssertTrue(progMinOS);
}

- (void)testEpoch_iOS_dyld_minos_at_least
{
    // Arrange: make binary built with iOS 13.0 (Fall 2019) SDK
    MockO main(MH_EXECUTE, "arm64", Platform::iOS, "13.0");
    TestState tester(main);

    // Act: test dyld_sdk_at_least()
    bool summer     = tester.apis.dyld_minos_at_least(main.header(), dyld_summer_2019_os_versions);
    bool fall       = tester.apis.dyld_minos_at_least(main.header(), dyld_fall_2019_os_versions);
    bool winter     = tester.apis.dyld_minos_at_least(main.header(), dyld_winter_2019_os_versions);
    bool summerFast = tester.apis.dyld_program_minos_at_least(dyld_summer_2019_os_versions);
    bool fallFast   = tester.apis.dyld_program_minos_at_least(dyld_fall_2019_os_versions);
    bool winterFast = tester.apis.dyld_program_minos_at_least(dyld_winter_2019_os_versions);

    // Assert: a binary built against iOS 13.0 SDK as built with at least fall 2019
    XCTAssertTrue(summer);
    XCTAssertTrue(fall);
    XCTAssertFalse(winter); // built with later iOS 13.3 sdk
    XCTAssertTrue(summerFast);
    XCTAssertTrue(fallFast);
    XCTAssertFalse(winterFast); // built with later iOS 13.3 sdk
}

- (void)testEpoch_macOS_dyld_minos_at_least
{
    // Arrange: make binary built with macOS 10.15 (Fall 2019) SDK
    MockO main(MH_EXECUTE, "arm64", Platform::macOS, "10.15");
    TestState tester(main);

    // Act: test dyld_sdk_at_least()
    bool summer = tester.apis.dyld_minos_at_least(main.header(), dyld_summer_2019_os_versions);
    bool fall   = tester.apis.dyld_minos_at_least(main.header(), dyld_fall_2019_os_versions);
    bool winter = tester.apis.dyld_minos_at_least(main.header(), dyld_winter_2019_os_versions);
    bool summerFast = tester.apis.dyld_program_minos_at_least(dyld_summer_2019_os_versions);
    bool fallFast   = tester.apis.dyld_program_minos_at_least(dyld_fall_2019_os_versions);
    bool winterFast = tester.apis.dyld_program_minos_at_least(dyld_winter_2019_os_versions);

    // Assert: a binary built against macOS 10.15 SDK as built with at least fall 2019
    XCTAssertTrue(summer);
    XCTAssertTrue(fall);
    XCTAssertFalse(winter); // built with later macOS 10.15.1 SDK
    XCTAssertTrue(summerFast);
    XCTAssertTrue(fallFast);
    XCTAssertFalse(winterFast); // built with later macOS 10.15.1 SDK
}

- (void)testEpoch_unknownPlatform_dyld_minos_at_least
{
    // Arrange: make binary built with an unknown (future) platform
    MockO main(MH_EXECUTE, "arm64", (dyld3::Platform)999, "10.15", "10.15");
    TestState tester(main);

    // Act: test dyld_sdk_at_least()
    bool summer = tester.apis.dyld_minos_at_least(main.header(), dyld_summer_2019_os_versions);
    bool fall   = tester.apis.dyld_minos_at_least(main.header(), dyld_fall_2019_os_versions);
    bool winter = tester.apis.dyld_minos_at_least(main.header(), dyld_winter_2019_os_versions);
    bool summerFast = tester.apis.dyld_program_minos_at_least(dyld_summer_2019_os_versions);
    bool fallFast   = tester.apis.dyld_program_minos_at_least(dyld_fall_2019_os_versions);
    bool winterFast = tester.apis.dyld_program_minos_at_least(dyld_winter_2019_os_versions);

    // Assert: a binary built against an unknown SDK will be newer than any known versions
    XCTAssertFalse(summer);
    XCTAssertFalse(fall);
    XCTAssertFalse(winter); // built with later macOS 10.15.1 SDK
    XCTAssertFalse(summerFast);
    XCTAssertFalse(fallFast);
    XCTAssertFalse(winterFast); // built with later macOS 10.15.1 SDK

}

- (void)testEpoch_bogusPlatformVersion_dyld_minos_at_least
{
    // Arrange: make binary built with macOS 11.0 (Fall 2020) SDK
    MockO main(MH_EXECUTE, "arm64", Platform::macOS, "10.12", "11.0");
    TestState tester(main);
    dyld_build_version_t bogusPlatformVersion = { .platform = 0xffff0000, .version = 1 };

    // Act: call dyld_minos_at_least with bogus platform and version
    bool sdk = tester.apis.dyld_minos_at_least(main.header(), bogusPlatformVersion);
    bool progSdk = tester.apis.dyld_program_minos_at_least(bogusPlatformVersion);

    // Assert: a bogus Platform version argument returns false
    XCTAssertFalse(sdk);
    XCTAssertFalse(progSdk);
}

- (void)testEpoch_NoBuildVersion_dyld_minos_at_least
{
    // Arrange: make binary built with no build version
    MockO main(MH_EXECUTE, "arm64");
    main.wrenchRemoveVersionInfo();
    TestState tester(main);

    // Act: call dyld_minos_at_least
    bool sdk = tester.apis.dyld_minos_at_least(main.header(), dyld_fall_2019_os_versions);
    bool progSdk = tester.apis.dyld_program_minos_at_least(dyld_fall_2019_os_versions);

    // Assert: no build version info returns false
    XCTAssertFalse(sdk);
    XCTAssertFalse(progSdk);
}

- (void)test_NSGetExecutablePathSymLink
{
    // Arrange: make a configuration with a path to main that is not real
    SyscallDelegate sys;
    sys._fileIDsToPath[SyscallDelegate::makeFsIdPair(0x1234,0x5678)] = "/bar/fileid.exe";
    TestState tester(sys, {}, {"executable_path=/foo/symlink.exe", "executable_file=0x1234,0x5678"});

    // Act: call _NSGetExecutablePath()
    char path[PATH_MAX];
    uint32_t pathBufSize = PATH_MAX;
    int result = tester.apis._NSGetExecutablePath(path, &pathBufSize);

    // Assert: result is buffer len used, path is symlink not real path (fileid.exe)
    XCTAssert(result == 0);
    XCTAssert(strcmp(path, "/foo/symlink.exe") == 0);
}

- (void)test_NSGetExecutablePathContainerBundle
{
    // Arrange: make a configuration with an executable_path in /var/containers/Bundle/Application
    MockO iOSMain(MH_EXECUTE, "arm64", Platform::iOS, "15.0");
    SyscallDelegate iOSSys;
    iOSSys._fileIDsToPath[SyscallDelegate::makeFsIdPair(0x1234,0x5678)] = "/private/var/containers/Bundle/Application/UUID/test.app/test";
    TestState iOSTester(iOSSys, iOSMain, {}, {"executable_file=0x1234,0x5678", "executable_path=/var/containers/Bundle/Application/UUID/test.app/test"});

    MockO macOSMain(MH_EXECUTE, "x86_64", Platform::macOS, "12.0");
    SyscallDelegate macOSSys;
    macOSSys._fileIDsToPath[SyscallDelegate::makeFsIdPair(0x1234,0x5678)] = "/private/var/containers/Bundle/Application/UUID/test.app/test";
    TestState macOSTester(macOSMain, {}, {"executable_file=0x1234,0x5678", "executable_path=/var/containers/Bundle/Application/UUID/test.app/test"});

    // Act: call _NSGetExecutablePath()
    uint32_t pathBufSize = PATH_MAX;

    char iOSPath[PATH_MAX];
    int iOSResult = iOSTester.apis._NSGetExecutablePath(iOSPath, &pathBufSize);

    char macOSPath[PATH_MAX];
    int macOSResult = macOSTester.apis._NSGetExecutablePath(macOSPath, &pathBufSize);

    // Assert: result is buffer len used, path has /private/ prefix
    XCTAssert(iOSResult == 0);
    XCTAssert(strcmp(iOSPath, "/private/var/containers/Bundle/Application/UUID/test.app/test") == 0);
    XCTAssert(macOSResult == 0);
    XCTAssert(strcmp(macOSPath, "/var/containers/Bundle/Application/UUID/test.app/test") == 0);
}
@end
