//
//  DyldProcessConfigTests.m
//  DyldProcessConfigTests
//
//  Created by Nick Kledzik on 8/21/20.
//

#import <XCTest/XCTest.h>
#include <mach-o/dyld_priv.h>
#include <_simple.h>

#include "DyldProcessConfig.h"
#include "MockO.h"

extern const dyld3::MachOAnalyzer __dso_handle;   // mach_header of this program

using dyld4::ProcessConfig;
using dyld4::KernelArgs;
using dyld4::SyscallDelegate;

typedef ProcessConfig::PathOverrides::Type  PathType;


@interface DyldProcessConfigTests : XCTestCase
@end



//
// The ProcessConfigTester is a utility for wrapping up the KernArgs and delegate needed
// to test the ProcessConfig class.
//
class ProcessConfigTester
{
public:
                                    ProcessConfigTester(MockO& main, const std::vector<const char*>& argv, const std::vector<const char*>& envp, const std::vector<const char*>& apple);
                                    ProcessConfigTester(MockO& main, const std::vector<const char*>& envp);
    void                            setArgs(const std::vector<const char*>& argv, const std::vector<const char*>& envp, const std::vector<const char*>& apple, bool addExe=true);
    void                            setInternalInstall(bool value)                  { _osDelegate._internalInstall = value;  if (!value) _osDelegate._commPageFlags.forceCustomerCache = true; }
    void                            setCWD(const char* path)                        { _osDelegate._cwd = path; }
    void                            setAMFI(uint64_t flags)                         { _osDelegate._amfiFlags = flags; }

    KernelArgs*           kernArgs()     { return &_kernArgs; }
    SyscallDelegate&      osDelegate()   { return _osDelegate; }

private:
    SyscallDelegate      _osDelegate;
    KernelArgs           _kernArgs;
};

ProcessConfigTester::ProcessConfigTester(MockO& main, const std::vector<const char*>& argv, const std::vector<const char*>& envp, const std::vector<const char*>& apple)
    : _kernArgs(main.header(), argv, envp, apple)
{
}

// used when the executable does not matter, just testing env vars
ProcessConfigTester::ProcessConfigTester(MockO& main, const std::vector<const char*>& envp)
    : _kernArgs(main.header(), {"test.exe"}, envp, {"executable_path=/foo/test.exe"})
{
    // use current dyld cache by default
    size_t len;
    _osDelegate._dyldCache = (DyldSharedCache*)_dyld_get_shared_cache_range(&len);
}


@implementation DyldProcessConfigTests

- (void)setUp {
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
}

- (void)testSimpleArgs
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"test.exe"}, {"TMPDIR=/tmp"}, {"junk=other", "executable_path=/foo/test.exe"});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig worked properly
    XCTAssert(testConfig.process.argc == 1);                                                    // "argc wrong
    XCTAssert(strcmp(testConfig.process.argv[0], "test.exe") == 0);                       // "argv[0] wrong
    XCTAssert(strcmp(testConfig.process.envp[0], "TMPDIR=/tmp") == 0);                    // "envp[0] wrong
    XCTAssert(strcmp(testConfig.process.apple[0], "junk=other") == 0);                    // "apple[0] wrong
    XCTAssert(strcmp(testConfig.process.apple[1], "executable_path=/foo/test.exe") == 0); // "apple[1] wrong
}


- (void)testBootArgsCustomerInstall
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"test.exe"}, {}, {"executable_path=/foo/test.exe", "dyld_flags=0xFFFFFF"});
    tester.osDelegate()._internalInstall = false;

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig is for customer install
    XCTAssertFalse(testConfig.process.commPage.testMode);             // test mode should not be true for non-internal installs
    XCTAssertTrue(testConfig.process.commPage.forceCustomerCache);    // prefer customer cache should be true for non-internal installs
}


- (void)testBootArgsInternalInstall
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"test.exe"}, {}, {"executable_path=/foo/test.exe", "dyld_flags=0xFFFFFF"});
    tester.osDelegate()._internalInstall = true;

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig is for internal install
    XCTAssertTrue(testConfig.process.commPage.testMode);               // test mode should be true for internal installs, given dyld_flags
    XCTAssertTrue(testConfig.process.commPage.forceCustomerCache);     // prefer customer cache should be true for internal installs, given dyld_flags
}


- (void)testMainPathWithExcutablePath
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"other.exe"}, {}, {"executable_path=/foo/test5.exe"});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig worked properly with executable_path set
    XCTAssert(strcmp(testConfig.process.mainExecutablePath, "/foo/test5.exe") == 0);
    XCTAssert(strcmp(testConfig.process.progname, "test5.exe") == 0);
}

- (void)testMainPathWithExecutableFileID
{
    // Arrange: mock up start up config with executable_file= set
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"/foo/argv0name"}, {}, {"executable_file=0x1234,0x5678"});
    tester.osDelegate()._fileIDsToPath[SyscallDelegate::makeFsIdPair(0x1234,0x5678)] = "/bar/fileid.exe";

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig converted executable_file to path
    XCTAssert(strcmp(testConfig.process.mainExecutablePath, "/bar/fileid.exe") == 0);
    XCTAssert(strcmp(testConfig.process.mainUnrealPath,     "/foo/argv0name") == 0);
    XCTAssert(strcmp(testConfig.process.progname,           "argv0name") == 0);
}

- (void)testMainPathWithExecutablePathAndDifferentFileID
{
    // Arrange: mock up start up config with executable_file= and executable_path= set
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"/foo/argv0name"}, {}, {"executable_path=/foo/symlink.exe", "executable_file=0x1234,0x5678"});
    tester.osDelegate()._fileIDsToPath[SyscallDelegate::makeFsIdPair(0x1234,0x5678)] = "/bar/fileid.exe";

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig converted executable_file to path, but kept symlink path for unreal path
    XCTAssert(strcmp(testConfig.process.mainExecutablePath, "/bar/fileid.exe") == 0);
    XCTAssert(strcmp(testConfig.process.mainUnrealPath,     "/foo/symlink.exe") == 0);
    XCTAssert(strcmp(testConfig.process.progname,           "symlink.exe") == 0);
}

- (void)testMainPathWithNoExcutablePath
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"/foo/argv0name"}, {}, {"other=stuff"});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig worked properly with no executable_path=, by falling back to argv[0]
    XCTAssert(strcmp(testConfig.process.mainExecutablePath, "/foo/argv0name") == 0);
    XCTAssert(strcmp(testConfig.process.progname, "argv0name") == 0);
}

- (void)testMainRelativePathWithNoExcutablePath
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"bar.exe"}, {}, {});
    tester.setCWD("/foo/dir");

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig worked properly with no executable_path=, by falling back to argv[0] and using cwd
    XCTAssert(strcmp(testConfig.process.mainExecutablePath, "/foo/dir/bar.exe") == 0);
    XCTAssert(strcmp(testConfig.process.progname, "bar.exe") == 0);
}

- (void)testMainPlatform_macOS
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS,  "11.0");
    ProcessConfigTester tester(main, {});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig worked properly to extract the platform from the binary
    XCTAssert(testConfig.process.platform == dyld3::Platform::macOS);
}

- (void)testMainPlatform_iOS
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64", Platform::iOS, "12.0");
    ProcessConfigTester tester(main, {});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig worked properly to extract the platform from the binary
    XCTAssert(testConfig.process.platform == dyld3::Platform::iOS);
}

- (void)testMainPlatform_Catalyst
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    main.customizeMakeZippered();
    ProcessConfigTester tester(main, {});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig worked properly to extract the platform from the binary
    XCTAssert(testConfig.process.platform == dyld3::Platform::iOSMac);
}

// on macOS, can force a macOS binary to be Catalyst, if it opts in
- (void)testPlatform_macOSForceToCatalyst
{
    // Arrange: mock up start up config with DYLD_FORCE_PLATFORM
    MockO main(MH_EXECUTE, "arm64", Platform::macOS, "10.15");
    main.customizeAddSection("__DATA", "__allow_alt_plat");
    ProcessConfigTester tester(main, {"DYLD_FORCE_PLATFORM=6"});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig worked properly to extract the platform from the binary
    XCTAssert(testConfig.process.platform == dyld3::Platform::iOSMac);
}

- (void)testPlatform_macOSForceToCatalystNoOptIn
{
    // Arrange: mock up start up config with DYLD_FORCE_PLATFORM
    MockO main(MH_EXECUTE, "arm64", Platform::macOS, "10.15");
    ProcessConfigTester tester(main, {"DYLD_FORCE_PLATFORM=6"});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig did not respect DYLD_FORCE_PLATFORM because binary did not opt-in
    XCTAssert(testConfig.process.platform == dyld3::Platform::macOS);
}


- (void)testArm64_32GradedArch
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64_32");
    ProcessConfigTester tester(main, {});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig worked properly to compute the graded set of archs that can be loaded
    XCTAssert(testConfig.process.archs == &dyld3::GradedArchs::arm64_32);
}

- (void)testArm6eGradedArchWithKeysOn
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64e");
    ProcessConfigTester tester(main, {});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig worked properly to compute the graded set of archs that can be loaded with keys off
    XCTAssert(testConfig.process.archs == &dyld3::GradedArchs::arm64e);
}

- (void)testArm6eGradedArchWithKeysOff
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64e");
    ProcessConfigTester tester(main, {"test.exe"}, {}, {"ptrauth_disabled=1", "executable_path=/foo/test.exe"});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig worked properly to compute the graded set of archs that can be loaded with keys off
    XCTAssert(testConfig.process.archs == &dyld3::GradedArchs::arm64e_keysoff);
}

- (void)testMacOSEnvVarNotPruned
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64", Platform::macOS, "10.15");
    ProcessConfigTester tester(main, {"OTHER=here", "DYLD_LIBRARY_PATH=/tmp", "DYLD_PRINT_LIBRARIES=1", "OTHER2=there"});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig preseved all env vars
    XCTAssert(testConfig.process.environ("OTHER") != nullptr);
    XCTAssert(testConfig.process.environ("DYLD_LIBRARY_PATH") != nullptr);
    XCTAssert(testConfig.process.environ("DYLD_PRINT_LIBRARIES") != nullptr);
    XCTAssert(testConfig.process.environ("OTHER2") != nullptr);
}

- (void)testMacOSEnvVarPruned
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64", Platform::macOS, "10.15");
    ProcessConfigTester tester(main, {"OTHER=here", "DYLD_LIBRARY_PATH=/tmp", "DYLD_PRINT_LIBRARIES=1", "OTHER2=there"});
    tester.setAMFI(0x29);    // turn off three flags

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig removed just DYLD_* env vars
    XCTAssert(testConfig.process.environ("OTHER") != nullptr);
    XCTAssert(testConfig.process.environ("DYLD_LIBRARY_PATH") == nullptr);
    XCTAssert(testConfig.process.environ("DYLD_PRINT_LIBRARIES") == nullptr);
    XCTAssert(testConfig.process.environ("OTHER2") != nullptr);
}


- (void)testAMFIsome
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {});
    tester.setAMFI(0x5555); // alternate bits on and off

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig worked properly to set alternate allow flags
    XCTAssertTrue( testConfig.security.allowAtPaths);
    XCTAssertFalse(testConfig.security.allowEnvVarsPath);
    XCTAssertTrue( testConfig.security.allowEnvVarsSharedCache);
    XCTAssertFalse(testConfig.security.allowClassicFallbackPaths);
    XCTAssertTrue( testConfig.security.allowEnvVarsPrint);
    XCTAssertFalse(testConfig.security.allowInsertFailures);
}


- (void)testAMFI_FAKE
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"test.exe"}, {"DYLD_AMFI_FAKE=0x5555"}, {"dyld_flags=0x2", "executable_path=/foo/test.exe"});
    tester.setInternalInstall(true); // enable test mode, so DYLD_AMFI_FAKE is checked
    tester.setAMFI(0xAAAA); // opposite of what DYLD_AMFI_FAKE overrides to

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig used DYLD_AMFI_FAKE because in test mode
    XCTAssertTrue( testConfig.security.allowAtPaths);
    XCTAssertFalse(testConfig.security.allowEnvVarsPath);
    XCTAssertTrue( testConfig.security.allowEnvVarsSharedCache);
    XCTAssertFalse(testConfig.security.allowClassicFallbackPaths);
    XCTAssertTrue( testConfig.security.allowEnvVarsPrint);
    XCTAssertFalse(testConfig.security.allowInsertFailures);
}

- (void)testAMFI_FAKE_disabled
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"test.exe"}, {"DYLD_AMFI_FAKE=0x5555"}, {"dyld_flags=0x2", "executable_path=/foo/test.exe"});
    tester.setInternalInstall(false); // disable test mode, so DYLD_AMFI_FAKE is not used
    tester.setAMFI(0xAAAA); // opposite of what DYLD_AMFI_FAKE overrides to

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig used DYLD_AMFI_FAKE because in test mode
    XCTAssertFalse(testConfig.security.allowAtPaths);
    XCTAssertTrue( testConfig.security.allowEnvVarsPath);
    XCTAssertFalse(testConfig.security.allowEnvVarsSharedCache);
    XCTAssertTrue( testConfig.security.allowClassicFallbackPaths);
    XCTAssertFalse(testConfig.security.allowEnvVarsPrint);
    XCTAssertTrue( testConfig.security.allowInsertFailures);
}

- (void)testLoggingNone
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig sets all logging to off by default
    XCTAssertFalse(testConfig.log.segments);
    XCTAssertFalse(testConfig.log.fixups);
    XCTAssertFalse(testConfig.log.initializers);
    XCTAssertFalse(testConfig.log.apis);
    XCTAssertFalse(testConfig.log.notifications);
    XCTAssertFalse(testConfig.log.interposing);
    XCTAssertFalse(testConfig.log.loaders);
    XCTAssertFalse(testConfig.log.libraries);
}

- (void)testLogging2
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"DYLD_PRINT_SEGMENTS=1", "DYLD_PRINT_BINDINGS=1",
                              "DYLD_PRINT_INITIALIZERS=1", "DYLD_PRINT_APIS=1",
                              "DYLD_PRINT_NOTIFICATIONS=1", "DYLD_PRINT_INTERPOSING=1",
                              "DYLD_PRINT_LOADERS=1", "DYLD_PRINT_LIBRARIES=1" });

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig sets all logging to true because of DYLD_PRINT_ env vars
    XCTAssertTrue(testConfig.log.segments);
    XCTAssertTrue(testConfig.log.fixups);
    XCTAssertTrue(testConfig.log.initializers);
    XCTAssertTrue(testConfig.log.apis);
    XCTAssertTrue(testConfig.log.notifications);
    XCTAssertTrue(testConfig.log.interposing);
    XCTAssertTrue(testConfig.log.loaders);
    XCTAssertTrue(testConfig.log.libraries);
}

- (void)testSkipMainDefault
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Asset: verify ProcessConfig sets skipMain to false by default
    XCTAssertFalse(testConfig.security.skipMain );
}

- (void)testSkipMainInternalDYLD_SKIP_MAIN
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"DYLD_SKIP_MAIN=1"});
    tester.setInternalInstall(true); // enable test mode

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig sees DYLD_SKIP_MAIN and sets skipMain to true
    XCTAssertTrue(testConfig.security.skipMain );
}

- (void)testSkipMainDYLD_SKIP_MAIN
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"DYLD_SKIP_MAIN=1"});
    tester.setInternalInstall(false); // disable test mode

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig ignores DYLD_SKIP_MAIN because this is not an internal install
    XCTAssertFalse(testConfig.security.skipMain );
}


- (void)testDyldCacheNone
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"DYLD_SHARED_REGION=avoid"});
    tester.setInternalInstall(true);

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig is configured to have no dyld shared cache
    XCTAssert(testConfig.dyldCache.addr == nullptr);
    XCTAssert(testConfig.dyldCache.path == nullptr);
    XCTAssert(testConfig.dyldCache.slide == 0);
}

/*
- (void)testDyldCacheCurrent
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig is configured to use current dyld shared cache
    size_t len;
    const DyldSharedCache* cache = (DyldSharedCache*)_dyld_get_shared_cache_range(&len);
    int64_t cacheSlide = (uint8_t*)cache - (uint8_t*)(cache->unslidLoadAddress());
    XCTAssert(testConfig.dyldCache() == cache);
    XCTAssert(strcmp(testConfig.dyldCachePath(), dyld_shared_cache_file_path()) == 0);
    XCTAssert(testConfig.dyldCacheSlide() == cacheSlide);
}
*/

#if 0
- (void)testBootToken
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"test.exe"}, {}, {"executable_boothash=0123456789ABCDef", "executable_path=/foo/test.exe"});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    STACK_ALLOC_ARRAY(uint8_t, bootToken, 128);
    bool bootTokenBuilt = testConfig.buildBootToken(bootToken);

    // Assert: verify ProcessConfig created boot token
    XCTAssert(bootTokenBuilt);
    XCTAssert(bootToken.count() == 24); // 8 from boothash, and 16 from dyld uuid
}

- (void)testBootTokenMissing
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    STACK_ALLOC_ARRAY(uint8_t, bootToken, 128);
    bool bootTokenBuilt = testConfig.buildBootToken(bootToken);

    // Assert: verify ProcessConfig could not create boot token because executable_boothash= missing
    XCTAssertFalse(bootTokenBuilt);
}

- (void)testDefaultPrebuiltLoaderSet
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig by default says don't look for or save PrebuiltLoaderSet
    //XCTAssertFalse(testConfig.saveAppClosureFile()); // FIXME requires new dyld cache format
    XCTAssertFalse(testConfig.failIfCouldBuildAppClosureFile());
}

- (void)testForceBuildPrebuiltLoaderSet
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"HOME=/foo/bar", "DYLD_USE_CLOSURES=1"});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig sees DYLD_USE_CLOSURES as building and saving PrebuildLoaderSet
//    XCTAssertTrue( testConfig.saveAppClosureFile());      // FIXME requires new dyld cache format
//    XCTAssertFalse(testConfig.lookForAppClosureFile());
}

- (void)testForceReadPrebuiltLoaderSet
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"HOME=/foo/bar", "DYLD_USE_CLOSURES=2"});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig sees DYLD_USE_CLOSURES as building and saving PrebuildLoaderSet
    XCTAssertFalse(testConfig.saveAppClosureFile());
//    XCTAssertTrue( testConfig.lookForAppClosureFile());
}

- (void)testBuildPrebuiltLoaderSetForContainerizedApps
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"test.exe"}, {"HOME=/private/var/mobile/Containers/Data/1234/"},
                               {"executable_boothash=0123456789ABCDef", "executable_path=/foo/test.exe"});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig sees this as a containerized app that loads/saves PrebuiltLoaderSet
//    XCTAssertTrue(testConfig.saveAppClosureFile());  // requires a dyld cache
//    XCTAssertTrue(testConfig.lookForAppClosureFile());
}

- (void)testDontBuildPrebuiltLoaderSetForContainerizedAppsWithDyldEnv
{
    // Arrange: mock up start up config
    MockO main(MH_EXECUTE, "arm64");
    ProcessConfigTester tester(main, {"test.exe"}, {"HOME=/private/var/mobile/Containers/Data/1234/", "DYLD_FRAMEWORK_PATH=/tmp"},
                               {"executable_boothash=0123456789ABCDef", "executable_path=/foo/test.exe"});

    // Act: run dyld's ProcessConfig constructor
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());

    // Assert: verify ProcessConfig disables saving/loading PrebuiltLoaderSet when DYLD_FRAMEWORK_PATH exists
    XCTAssertFalse(testConfig.saveAppClosureFile());
//    XCTAssertFalse(testConfig.lookForAppClosureFile());
}
#endif




- (void)testDefaultFallbackFrameworkPathsMac
{
    // Arrange: set up PathOverrides object
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    ProcessConfigTester tester(main, {});

    // Act: record each path searched
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    __block std::vector<std::string> pathsSearched;
    __block std::vector<PathType>    pathType;
    bool                             stop = false;
    testConfig.pathOverrides.forEachPathVariant("/stuff/Foo.framework/Foo", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        pathsSearched.push_back(possiblePath);
        pathType.push_back(type);
    });

    // Assert: verify original path is first, then fallback paths
    XCTAssert(pathsSearched.size() == 3);
    XCTAssert(pathsSearched[0] == "/stuff/Foo.framework/Foo");
    XCTAssert(pathsSearched[1] == "/Library/Frameworks/Foo.framework/Foo");
    XCTAssert(pathsSearched[2] == "/System/Library/Frameworks/Foo.framework/Foo");
    XCTAssert(pathType[0] == ProcessConfig::PathOverrides::Type::rawPath);
    XCTAssert(pathType[1] == ProcessConfig::PathOverrides::Type::standardFallback);
    XCTAssert(pathType[2] == ProcessConfig::PathOverrides::Type::standardFallback);
}

- (void)testDefaultFallbackFrameworkPaths
{
    // Arrange: set up PathOverrides object
    MockO main(MH_EXECUTE, "arm64", Platform::iOS, "13.0");
    ProcessConfigTester tester(main, {});

    // Act: record each path searched
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    __block std::vector<std::string> pathsSearched;
    __block std::vector<PathType>    pathType;
    bool                             stop = false;
    testConfig.pathOverrides.forEachPathVariant("/stuff/Foo.framework/Foo", dyld3::Platform::iOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        pathsSearched.push_back(possiblePath);
        pathType.push_back(type);
    });

    // Assert: verify original path is first, then /System/Library
    XCTAssert(pathsSearched.size() == 2);
    XCTAssert(pathsSearched[0] == "/stuff/Foo.framework/Foo");
    XCTAssert(pathsSearched[1] == "/System/Library/Frameworks/Foo.framework/Foo");
    XCTAssert(pathType[0] == ProcessConfig::PathOverrides::Type::rawPath);
    XCTAssert(pathType[1] == ProcessConfig::PathOverrides::Type::standardFallback);
}

- (void)testDefaultFallbackFrameworkPathRepeats
{
    // Arrange: set up PathOverrides object
    MockO main(MH_EXECUTE, "arm64", Platform::iOS, "13.0");
    ProcessConfigTester tester(main, {});

    // Act: record each path searched
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    __block std::vector<std::string> pathsSearched;
    __block std::vector<PathType>    pathType;
    bool                             stop = false;
    testConfig.pathOverrides.forEachPathVariant("/System/Library/Frameworks/Foo.framework/Foo", dyld3::Platform::iOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        pathsSearched.push_back(possiblePath);
        pathType.push_back(type);
    });

    // Assert: verify fallback is not used because original path is a fallback search location
    XCTAssert(pathsSearched.size() == 1);
    XCTAssert(pathsSearched[0] == "/System/Library/Frameworks/Foo.framework/Foo");
    XCTAssert(pathType[0] == ProcessConfig::PathOverrides::Type::rawPath);
}

- (void)testCustomFallbackFrameworkPaths
{
    // Arrange: set up PathOverrides object
    MockO main(MH_EXECUTE, "arm64", Platform::iOS, "13.0");
    ProcessConfigTester tester(main, {"DYLD_FALLBACK_FRAMEWORK_PATHS=/yonder:/hither"});

    // Act: record each path searched
     ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    __block std::vector<std::string> pathsSearched;
    __block std::vector<PathType>    pathType;
    bool                             stop = false;
    testConfig.pathOverrides.forEachPathVariant("/stuff/Foo.framework/Foo", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        pathsSearched.push_back(possiblePath);
        pathType.push_back(type);
    });

    // Assert: verify original path is first, then fallback paths
    XCTAssert(pathsSearched.size() == 3);
    XCTAssert(pathsSearched[0] == "/stuff/Foo.framework/Foo");
    XCTAssert(pathsSearched[1] == "/yonder/Foo.framework/Foo");
    XCTAssert(pathsSearched[2] == "/hither/Foo.framework/Foo");
    XCTAssert(pathType[0] == ProcessConfig::PathOverrides::Type::rawPath);
    XCTAssert(pathType[1] == ProcessConfig::PathOverrides::Type::customFallback);
    XCTAssert(pathType[2] == ProcessConfig::PathOverrides::Type::customFallback);
}

- (void)testDefaultFallbackLibraryPaths
{
    // Arrange: set up PathOverrides object
    MockO main(MH_EXECUTE, "arm64", Platform::iOS, "13.0");
    ProcessConfigTester tester(main, {});

    // Act: record each path searched
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    __block std::vector<std::string> pathsSearched;
    __block std::vector<PathType>    pathType;
    bool                             stop = false;
    testConfig.pathOverrides.forEachPathVariant("/stuff/libfoo.dylib", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        pathsSearched.push_back(possiblePath);
        pathType.push_back(type);
    });

    // Assert: verify original path is first, then fallback paths
    XCTAssert(pathsSearched.size() == 3);
    XCTAssert(pathsSearched[0] == "/stuff/libfoo.dylib");
    XCTAssert(pathsSearched[1] == "/usr/local/lib/libfoo.dylib");
    XCTAssert(pathsSearched[2] == "/usr/lib/libfoo.dylib");
    XCTAssert(pathType[0] == ProcessConfig::PathOverrides::Type::rawPath);
    XCTAssert(pathType[1] == ProcessConfig::PathOverrides::Type::standardFallback);
    XCTAssert(pathType[2] == ProcessConfig::PathOverrides::Type::standardFallback);
}


- (void)testCustomFallbackLibraryPaths
{
    // Arrange: set up PathOverrides object
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    ProcessConfigTester tester(main, {"DYLD_FALLBACK_LIBRARY_PATHS=/yonder:/hither"});

    // Act: record each path searched
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    __block std::vector<std::string> pathsSearched;
    __block std::vector<PathType>    pathType;
    bool                             stop = false;
    testConfig.pathOverrides.forEachPathVariant("/stuff/libfoo.dylib", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        pathsSearched.push_back(possiblePath);
        pathType.push_back(type);
    });

    // Assert: verify original path is first, then fallback paths
    XCTAssert(pathsSearched.size() == 3);
    XCTAssert(pathsSearched[0] == "/stuff/libfoo.dylib");
    XCTAssert(pathsSearched[1] == "/yonder/libfoo.dylib");
    XCTAssert(pathsSearched[2] == "/hither/libfoo.dylib");
    XCTAssert(pathType[0] == ProcessConfig::PathOverrides::Type::rawPath);
    XCTAssert(pathType[1] == ProcessConfig::PathOverrides::Type::customFallback);
    XCTAssert(pathType[2] == ProcessConfig::PathOverrides::Type::customFallback);
}

- (void)testCustomNoFallbackLibraryPaths
{
    // Arrange: set up PathOverrides object
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    ProcessConfigTester tester(main, {"DYLD_FALLBACK_LIBRARY_PATHS=/yonder:/hither"});

    // Act: record each path searched
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    __block std::vector<std::string> pathsSearched;
    __block std::vector<PathType>    pathType;
    bool                             stop = false;
    testConfig.pathOverrides.forEachPathVariant("/stuff/libfoo.dylib", dyld3::Platform::macOS, true, stop, ^(const char* possiblePath, PathType type, bool&) {
        pathsSearched.push_back(possiblePath);
        pathType.push_back(type);
    });

    // Assert: verify original path is first, an no fallbacks are searched
    XCTAssert(pathsSearched.size() == 3);
    XCTAssert(pathsSearched[0] == "/stuff/libfoo.dylib");
    XCTAssert(pathsSearched[1] == "/usr/local/lib/libfoo.dylib");
    XCTAssert(pathsSearched[2] == "/usr/lib/libfoo.dylib");
    XCTAssert(pathType[0] == ProcessConfig::PathOverrides::Type::rawPath);
    XCTAssert(pathType[1] == ProcessConfig::PathOverrides::Type::standardFallback);
    XCTAssert(pathType[2] == ProcessConfig::PathOverrides::Type::standardFallback);
}

- (void)testVersionedDylibs
{
    // Arrange: set up PathOverrides two search dirs and six dylibs
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    ProcessConfigTester tester(main, {"DYLD_VERSIONED_LIBRARY_PATHS=/yonder:/hither"});
    tester.osDelegate()._dirMap["/yonder"].push_back("libA.dylib");
    tester.osDelegate()._dirMap["/yonder"].push_back("libB.dylib");
    tester.osDelegate()._dirMap["/yonder"].push_back("libC.dylib");
    tester.osDelegate()._dirMap["/hither"].push_back("libD.dylib");
    tester.osDelegate()._dirMap["/hither"].push_back("libE.dylib");
    tester.osDelegate()._dirMap["/hither"].push_back("libF.dylib");
    tester.osDelegate()._dylibInfoMap["/usr/lib/libA.dylib"] = {2, "/usr/lib/libA.dylib"};
    tester.osDelegate()._dylibInfoMap["/usr/lib/libB.dylib"] = {4, "/usr/lib/libB.dylib"};
    tester.osDelegate()._dylibInfoMap["/usr/lib/libC.dylib"] = {6, "/usr/lib/libC.dylib"};
    tester.osDelegate()._dylibInfoMap["/usr/lib/libD.dylib"] = {3, "/usr/lib/libD.dylib"};
    tester.osDelegate()._dylibInfoMap["/usr/lib/libE.dylib"] = {5, "/usr/lib/libE.dylib"};
    tester.osDelegate()._dylibInfoMap["/usr/lib/libF.dylib"] = {7, "/usr/lib/libF.dylib"};
    tester.osDelegate()._dylibInfoMap["/yonder/libA.dylib"]  = {4, "/usr/lib/libA.dylib"};   // this will override 4 > 2
    tester.osDelegate()._dylibInfoMap["/yonder/libB.dylib"]  = {4, "/usr/lib/libB.dylib"};
    tester.osDelegate()._dylibInfoMap["/yonder/libC.dylib"]  = {4, "/usr/lib/libC.dylib"};
    tester.osDelegate()._dylibInfoMap["/hither/libD.dylib"]  = {5, "/usr/lib/libD.dylib"};   // this will override 5 > 3
    tester.osDelegate()._dylibInfoMap["/hither/libE.dylib"]  = {5, "/usr/lib/libE.dylib"};
    tester.osDelegate()._dylibInfoMap["/hither/libF.dylib"]  = {5, "/usr/lib/libF.dylib"};


    // Act: record if overrides found
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    bool         stop = false;
    __block bool foundYonderA = false;
    __block bool foundHitherA = false;
    testConfig.pathOverrides.forEachPathVariant("/usr/lib/libA.dylib", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/yonder/libA.dylib") == 0 )
            foundYonderA = true;
        if ( strcmp(possiblePath, "/hither/libA.dylib") == 0 )
            foundHitherA = true;
    });
    __block bool foundYonderB = false;
    __block bool foundHitherB = false;
    testConfig.pathOverrides.forEachPathVariant("/usr/lib/libB.dylib", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/yonder/libB.dylib") == 0 )
            foundYonderB = true;
        if ( strcmp(possiblePath, "/hither/libB.dylib") == 0 )
            foundHitherB = true;
    });
    __block bool foundYonderC = false;
    __block bool foundHitherC = false;
    testConfig.pathOverrides.forEachPathVariant("/usr/lib/libC.dylib", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/yonder/libC.dylib") == 0 )
            foundYonderC = true;
        if ( strcmp(possiblePath, "/hither/libC.dylib") == 0 )
            foundHitherC = true;
    });
    __block bool foundYonderD = false;
    __block bool foundHitherD = false;
    testConfig.pathOverrides.forEachPathVariant("/usr/lib/libD.dylib", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/yonder/libD.dylib") == 0 )
            foundYonderD = true;
        if ( strcmp(possiblePath, "/hither/libD.dylib") == 0 )
            foundHitherD = true;
    });
    __block bool foundYonderE = false;
    __block bool foundHitherE = false;
    testConfig.pathOverrides.forEachPathVariant("/usr/lib/libE.dylib", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/yonder/libE.dylib") == 0 )
            foundYonderE = true;
        if ( strcmp(possiblePath, "/hither/libE.dylib") == 0 )
            foundHitherE = true;
    });
    __block bool foundYonderF = false;
    __block bool foundHitherF = false;
    testConfig.pathOverrides.forEachPathVariant("/usr/lib/libF.dylib", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/yonder/libF.dylib") == 0 )
            foundYonderF = true;
        if ( strcmp(possiblePath, "/hither/libF.dylib") == 0 )
            foundHitherF = true;
    });


    // Assert: verify original path is first, then fallback paths
    XCTAssertTrue(foundYonderA);    // yonder/libA.dylib is used because it has version 4 which is > /usr/lib/libA.dylib which has version 2
    XCTAssertFalse(foundHitherA);
    XCTAssertFalse(foundYonderB);   // note both yonder/libB.dylib and usr/lib/libB.dylib have version 4, so use one from OS
    XCTAssertFalse(foundHitherB);
    XCTAssertFalse(foundYonderC);
    XCTAssertFalse(foundHitherC);
    XCTAssertFalse(foundYonderD);
    XCTAssertTrue(foundHitherD);    // hither/libD.dylib is used because it has version 5 which is > /usr/lib/libA.dylib which has version 3
    XCTAssertFalse(foundYonderE);
    XCTAssertFalse(foundHitherE);   // note both hither/libE.dylib and usr/lib/libE.dylib have version 5, so use one from OS
    XCTAssertFalse(foundYonderF);
    XCTAssertFalse(foundHitherF);
}

- (void)testVersionedDylibsMissing
{
    // Arrange: set up PathOverrides with a versioned dir with a dylib that overrides an OS dylib that does not exist
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    ProcessConfigTester tester(main, {"DYLD_VERSIONED_LIBRARY_PATHS=/newstuff"});
    tester.osDelegate()._dirMap["/newstuff"].push_back("libNew.dylib");
    tester.osDelegate()._dylibInfoMap["/newstuff/libNew.dylib"]  = {1, "/usr/lib/libNew.dylib"};

    // Act: record if overrides found
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    bool         stop          = false;
    __block bool foundNewStuff = false;
    testConfig.pathOverrides.forEachPathVariant("/usr/lib/libNew.dylib", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "new path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/newstuff/libNew.dylib") == 0 )
            foundNewStuff = true;
    });

    // Assert: verify dylib from versioned path is used
    XCTAssertTrue(foundNewStuff);
}

- (void)testVersionedDylibsDups
{
    // Arrange: set up PathOverrides so both dirs have libA and libB, but versions determine which to use
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    ProcessConfigTester tester(main, {"DYLD_VERSIONED_LIBRARY_PATHS=/alt1:/alt2"});
    tester.osDelegate()._dirMap["/alt1"].push_back("libA.dylib");
    tester.osDelegate()._dirMap["/alt1"].push_back("libB.dylib");
    tester.osDelegate()._dirMap["/alt2"].push_back("libA.dylib");
    tester.osDelegate()._dirMap["/alt2"].push_back("libB.dylib");
    tester.osDelegate()._dylibInfoMap["/usr/lib/libA.dylib"] = {5, "/usr/lib/libA.dylib"};
    tester.osDelegate()._dylibInfoMap["/alt1/libA.dylib"]    = {6, "/usr/lib/libA.dylib"};
    tester.osDelegate()._dylibInfoMap["/alt2/libA.dylib"]    = {7, "/usr/lib/libA.dylib"};   // alt2=7 which is newer that alt1=6 and OS=5
    tester.osDelegate()._dylibInfoMap["/usr/lib/libB.dylib"] = {5, "/usr/lib/libB.dylib"};
    tester.osDelegate()._dylibInfoMap["/alt1/libB.dylib"]    = {7, "/usr/lib/libB.dylib"};   // alt1=7 which is newer that alt2=6 and OS=5
    tester.osDelegate()._dylibInfoMap["/alt2/libB.dylib"]    = {6, "/usr/lib/libB.dylib"};

    // Act: record if overrides found
    bool         stop       = false;
    __block bool foundAlt1A = false;
    __block bool foundAlt2A = false;
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    testConfig.pathOverrides.forEachPathVariant("/usr/lib/libA.dylib", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "dup path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/alt1/libA.dylib") == 0 )
            foundAlt1A = true;
        if ( strcmp(possiblePath, "/alt2/libA.dylib") == 0 )
            foundAlt2A = true;
    });
    __block bool foundAlt1B = false;
    __block bool foundAlt2B = false;
    testConfig.pathOverrides.forEachPathVariant("/usr/lib/libB.dylib", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "dup path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/alt1/libB.dylib") == 0 )
            foundAlt1B = true;
        if ( strcmp(possiblePath, "/alt2/libB.dylib") == 0 )
            foundAlt2B = true;
    });


    // Assert: verify original path is first, then fallback paths
    XCTAssertFalse(foundAlt1A);
    XCTAssertTrue( foundAlt2A);
    XCTAssertTrue( foundAlt1B);
    XCTAssertFalse(foundAlt2B);
}


- (void)testVersionedFrameworks
{
    // Arrange: set up PathOverrides two search dirs and six
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    ProcessConfigTester tester(main, {"DYLD_VERSIONED_FRAMEWORK_PATHS=/yonder:/hither"});
    tester.osDelegate()._dirMap["/yonder"].push_back("A.framework");
    tester.osDelegate()._dirMap["/yonder"].push_back("B.framework");
    tester.osDelegate()._dirMap["/yonder"].push_back("C.framework");
    tester.osDelegate()._dirMap["/hither"].push_back("D.framework");
    tester.osDelegate()._dirMap["/hither"].push_back("E.framework");
    tester.osDelegate()._dirMap["/hither"].push_back("F.framework");
    tester.osDelegate()._dylibInfoMap["/System/Library/PrivateFrameworks/A.framework/A"] = {2, "/System/Library/PrivateFrameworks/A.framework/A"};
    tester.osDelegate()._dylibInfoMap["/System/Library/PrivateFrameworks/B.framework/B"] = {4, "/System/Library/PrivateFrameworks/B.framework/B"};
    tester.osDelegate()._dylibInfoMap["/System/Library/PrivateFrameworks/C.framework/C"] = {6, "/System/Library/PrivateFrameworks/C.framework/C"};
    tester.osDelegate()._dylibInfoMap["/System/Library/PrivateFrameworks/D.framework/D"] = {3, "/System/Library/PrivateFrameworks/D.framework/D"};
    tester.osDelegate()._dylibInfoMap["/System/Library/PrivateFrameworks/E.framework/E"] = {5, "/System/Library/PrivateFrameworks/E.framework/E"};
    tester.osDelegate()._dylibInfoMap["/System/Library/PrivateFrameworks/F.framework/F"] = {7, "/System/Library/PrivateFrameworks/F.framework/F"};
    tester.osDelegate()._dylibInfoMap["/yonder/A.framework/A"]  = {4, "/System/Library/PrivateFrameworks/A.framework/A"};   // this will override 4 > 2
    tester.osDelegate()._dylibInfoMap["/yonder/B.framework/B"]  = {4, "/System/Library/PrivateFrameworks/B.framework/B"};
    tester.osDelegate()._dylibInfoMap["/yonder/C.framework/C"]  = {4, "/System/Library/PrivateFrameworks/C.framework/C"};
    tester.osDelegate()._dylibInfoMap["/hither/D.framework/D"]  = {5, "/System/Library/PrivateFrameworks/D.framework/D"};   // this will override 5 > 3
    tester.osDelegate()._dylibInfoMap["/hither/E.framework/E"]  = {5, "/System/Library/PrivateFrameworks/E.framework/E"};
    tester.osDelegate()._dylibInfoMap["/hither/F.framework/F"]  = {5, "/System/Library/PrivateFrameworks/F.framework/F"};


    // Act: record if overrides found
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    bool         stop         = false;
    __block bool foundYonderA = false;
    __block bool foundHitherA = false;
    testConfig.pathOverrides.forEachPathVariant("/System/Library/PrivateFrameworks/A.framework/A", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/yonder/A.framework/A") == 0 )
            foundYonderA = true;
        if ( strcmp(possiblePath, "/hither/A.framework/A") == 0 )
            foundHitherA = true;
    });
    __block bool foundYonderB = false;
    __block bool foundHitherB = false;
    __block bool foundBaseB = false;
    testConfig.pathOverrides.forEachPathVariant("/System/Library/PrivateFrameworks/B.framework/B", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/yonder/B.framework/B") == 0 )
            foundYonderB = true;
        if ( strcmp(possiblePath, "/hither/B.framework/B") == 0 )
            foundHitherB = true;
        if ( strcmp(possiblePath, "/System/Library/PrivateFrameworks/B.framework/B") == 0 )
            foundBaseB = true;
    });
    __block bool foundYonderC = false;
    __block bool foundHitherC = false;
    __block bool foundBaseC = false;
    testConfig.pathOverrides.forEachPathVariant("/System/Library/PrivateFrameworks/C.framework/C", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/yonder/C.framework/C") == 0 )
            foundYonderC = true;
        if ( strcmp(possiblePath, "/hither/C.framework/C") == 0 )
            foundHitherC = true;
        if ( strcmp(possiblePath, "/System/Library/PrivateFrameworks/C.framework/C") == 0 )
            foundBaseC = true;
    });
    __block bool foundYonderD = false;
    __block bool foundHitherD = false;
    testConfig.pathOverrides.forEachPathVariant("/System/Library/PrivateFrameworks/D.framework/D", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/yonder/D.framework/D") == 0 )
            foundYonderD = true;
        if ( strcmp(possiblePath, "/hither/D.framework/D") == 0 )
            foundHitherD = true;
    });
    __block bool foundYonderE = false;
    __block bool foundHitherE = false;
    __block bool foundBaseE = false;
    testConfig.pathOverrides.forEachPathVariant("/System/Library/PrivateFrameworks/E.framework/E", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/yonder/E.framework/E") == 0 )
            foundYonderE = true;
        if ( strcmp(possiblePath, "/hither/E.framework/E") == 0 )
            foundHitherE = true;
        if ( strcmp(possiblePath, "/System/Library/PrivateFrameworks/E.framework/E") == 0 )
            foundBaseE = true;
    });
    __block bool foundYonderF = false;
    __block bool foundHitherF = false;
    __block bool foundBaseF = false;
    testConfig.pathOverrides.forEachPathVariant("/System/Library/PrivateFrameworks/F.framework/F", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/yonder/F.framework/F") == 0 )
            foundYonderF = true;
        if ( strcmp(possiblePath, "/hither/F.framework/F") == 0 )
            foundHitherF = true;
        if ( strcmp(possiblePath, "/System/Library/PrivateFrameworks/F.framework/F") == 0 )
            foundBaseF = true;
    });


    // Assert: verify original path is first, then fallback paths
    XCTAssertTrue(foundYonderA);    // yonder/libA.dylib is used because it has version 4 which is > /usr/lib/libA.dylib which has version 2
    XCTAssertFalse(foundHitherA);
    XCTAssertFalse(foundYonderB);   // note both yonder/libB.dylib and usr/lib/libB.dylib have version 4, so use one from OS
    XCTAssertFalse(foundHitherB);
    XCTAssertTrue(foundBaseB);
    XCTAssertFalse(foundYonderC);
    XCTAssertFalse(foundHitherC);
    XCTAssertTrue(foundBaseC);
    XCTAssertFalse(foundYonderD);
    XCTAssertTrue(foundHitherD);    // hither/libD.dylib is used because it has version 5 which is > /usr/lib/libA.dylib which has version 3
    XCTAssertFalse(foundYonderE);
    XCTAssertFalse(foundHitherE);   // note both hither/libE.dylib and usr/lib/libE.dylib have version 5, so use one from OS
    XCTAssertTrue(foundBaseE);
    XCTAssertFalse(foundYonderF);
    XCTAssertFalse(foundHitherF);
    XCTAssertTrue(foundBaseF);
}

- (void)testVersionedFrameworksMissing
{
    // Arrange: set up PathOverrides with a versioned dir with a framework that overrides an OS framework that does not exist
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    ProcessConfigTester tester(main, {"DYLD_VERSIONED_FRAMEWORK_PATHS=/newstuff"});
    tester.osDelegate()._dirMap["/newstuff"].push_back("New.framework");
    tester.osDelegate()._dylibInfoMap["/newstuff/New.framework/New"]  = {1, "/System/Library/PrivateFrameworks/New.framework/New"};

    // Act: record if overrides found
    bool         stop          = false;
    __block bool foundNewStuff = false;
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    testConfig.pathOverrides.forEachPathVariant("/System/Library/PrivateFrameworks/New.framework/New", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "new path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/newstuff/New.framework/New") == 0 )
            foundNewStuff = true;
    });

    // Assert: verify dylib from versioned path is used
    XCTAssertTrue(foundNewStuff);
}

- (void)testVersionedFrameworksDups
{
    // Arrange: set up PathOverrides so both dirs have A.framework and B.framework, but versions determine which to use
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    ProcessConfigTester tester(main, {"DYLD_VERSIONED_FRAMEWORK_PATHS=/alt1:/alt2"});
    tester.osDelegate()._dirMap["/alt1"].push_back("A.framework");
    tester.osDelegate()._dirMap["/alt1"].push_back("B.framework");
    tester.osDelegate()._dirMap["/alt2"].push_back("A.framework");
    tester.osDelegate()._dirMap["/alt2"].push_back("B.framework");
    tester.osDelegate()._dylibInfoMap["/System/Library/PrivateFrameworks/A.framework/A"]  = {5, "/System/Library/PrivateFrameworks/A.framework/A"};
    tester.osDelegate()._dylibInfoMap["/alt1/A.framework/A"]                              = {6, "/System/Library/PrivateFrameworks/A.framework/A"};
    tester.osDelegate()._dylibInfoMap["/alt2/A.framework/A"]                              = {7, "/System/Library/PrivateFrameworks/A.framework/A"};  // alt2=7 which is newer that alt1=6 and OS=5
    tester.osDelegate()._dylibInfoMap["/System/Library/PrivateFrameworks/B.framework/B"]  = {5, "/System/Library/PrivateFrameworks/B.framework/B"};
    tester.osDelegate()._dylibInfoMap["/alt1/B.framework/B"]                              = {7, "/System/Library/PrivateFrameworks/B.framework/B"};  // alt1=7 which is newer that alt2=6 and OS=5
    tester.osDelegate()._dylibInfoMap["/alt2/B.framework/B"]                              = {6, "/System/Library/PrivateFrameworks/B.framework/B"};

    // Act: record if overrides found
    bool         stop       = false;
    __block bool foundAlt1A = false;
    __block bool foundAlt2A = false;
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    testConfig.pathOverrides.forEachPathVariant("/System/Library/PrivateFrameworks/A.framework/A", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "dup path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/alt1/A.framework/A") == 0 )
            foundAlt1A = true;
        if ( strcmp(possiblePath, "/alt2/A.framework/A") == 0 )
            foundAlt2A = true;
    });
    __block bool foundAlt1B = false;
    __block bool foundAlt2B = false;
    testConfig.pathOverrides.forEachPathVariant("/System/Library/PrivateFrameworks/B.framework/B", dyld3::Platform::macOS, false, stop, ^(const char* possiblePath, PathType type, bool&) {
        //fprintf(stderr, "dup path: %s\n", possiblePath);
        if ( strcmp(possiblePath, "/alt1/B.framework/B") == 0 )
            foundAlt1B = true;
        if ( strcmp(possiblePath, "/alt2/B.framework/B") == 0 )
            foundAlt2B = true;
    });


    // Assert: verify original path is first, then fallback paths
    XCTAssertFalse(foundAlt1A);
    XCTAssertTrue( foundAlt2A);
    XCTAssertTrue( foundAlt1B);
    XCTAssertFalse(foundAlt2B);
}

- (void)testLibraryPathLC
{
    // Arrange: Add DYLD_LIBRARY_PATH via LC_DYLD_ENVIRONMENT
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    main.customizeAddDyldEnvVar("DYLD_LIBRARY_PATH=/foo/bar");
    ProcessConfigTester tester(main, {});

    // Act: record if we can use a prebuilt loader
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    bool dontUsePrebuiltForApp = testConfig.pathOverrides.dontUsePrebuiltForApp();

    // Assert: We should be able to use prebuilt loaders using DYLD_LIBRARY_PATH in LC_DYLD_ENVIRONMENT
    XCTAssertFalse(dontUsePrebuiltForApp);
}

- (void)testFrameworkPathLC
{
    // Arrange: Add DYLD_FRAMEWORK_PATH via LC_DYLD_ENVIRONMENT
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    main.customizeAddDyldEnvVar("DYLD_FRAMEWORK_PATH=/foo/bar");
    ProcessConfigTester tester(main, {});

    // Act: record if we can use a prebuilt loader
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    bool dontUsePrebuiltForApp = testConfig.pathOverrides.dontUsePrebuiltForApp();

    // Assert: We should be able to use prebuilt loaders using DYLD_FRAMEWORK_PATH in LC_DYLD_ENVIRONMENT
    XCTAssertFalse(dontUsePrebuiltForApp);
}

- (void)testFallbackLibraryPathLC
{
    // Arrange: Add DYLD_FALLBACK_LIBRARY_PATH via LC_DYLD_ENVIRONMENT
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    main.customizeAddDyldEnvVar("DYLD_FALLBACK_LIBRARY_PATH=/foo/bar");
    ProcessConfigTester tester(main, {});

    // Act: record if we can use a prebuilt loader
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    bool dontUsePrebuiltForApp = testConfig.pathOverrides.dontUsePrebuiltForApp();

    // Assert: We should be able to use prebuilt loaders using DYLD_FALLBACK_LIBRARY_PATH in LC_DYLD_ENVIRONMENT
    XCTAssertFalse(dontUsePrebuiltForApp);
}

- (void)testFallbackFrameworkPathLC
{
    // Arrange: Add DYLD_FALLBACK_FRAMEWORK_PATH via LC_DYLD_ENVIRONMENT
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    main.customizeAddDyldEnvVar("DYLD_FALLBACK_FRAMEWORK_PATH=/foo/bar");
    ProcessConfigTester tester(main, {});

    // Act: record if we can use a prebuilt loader
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    bool dontUsePrebuiltForApp = testConfig.pathOverrides.dontUsePrebuiltForApp();

    // Assert: We should be able to use prebuilt loaders using DYLD_FALLBACK_FRAMEWORK_PATH in LC_DYLD_ENVIRONMENT
    XCTAssertFalse(dontUsePrebuiltForApp);
}

- (void)testVersionedLibraryPathLC
{
    // Arrange: Add DYLD_VERSIONED_LIBRARY_PATH via LC_DYLD_ENVIRONMENT
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    main.customizeAddDyldEnvVar("DYLD_VERSIONED_LIBRARY_PATH=/foo/bar");
    ProcessConfigTester tester(main, {});

    // Act: record if we can use a prebuilt loader
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    bool dontUsePrebuiltForApp = testConfig.pathOverrides.dontUsePrebuiltForApp();

    // Assert: We should not be able to use prebuilt loaders using LC_DYLD_ENVIRONMENT
    XCTAssertTrue(dontUsePrebuiltForApp);
}

- (void)testVersionedFrameworkPathLC
{
    // Arrange: Add DYLD_VERSIONED_FRAMEWORK_PATH via LC_DYLD_ENVIRONMENT
    MockO main(MH_EXECUTE, "x86_64", Platform::macOS, "10.15");
    main.customizeAddDyldEnvVar("DYLD_VERSIONED_FRAMEWORK_PATH=/foo/bar");
    ProcessConfigTester tester(main, {});

    // Act: record if we can use a prebuilt loader
    ProcessConfig testConfig(tester.kernArgs(), tester.osDelegate());
    bool dontUsePrebuiltForApp = testConfig.pathOverrides.dontUsePrebuiltForApp();

    // Assert: We should not be able to use prebuilt loaders using LC_DYLD_ENVIRONMENT
    XCTAssertTrue(dontUsePrebuiltForApp);
}

// need tests that use LC_DYLD_ENVIRONMENT
// need tests that have VERSIONED_PATH=@loader_path and @executable_path/
// need tests that use colon seperated list of @paths

@end
