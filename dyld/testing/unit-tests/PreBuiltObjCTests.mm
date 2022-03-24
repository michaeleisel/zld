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

#import <XCTest/XCTest.h>
#include <mach-o/dyld_priv.h>
#include <_simple.h>

#include "JustInTimeLoader.h"
#include "PrebuiltObjC.h"
#include "DyldRuntimeState.h"
#include "MockO.h"

#include "objc-shared-cache.h"
#include "OptimizerObjC.h"
#include "PerfectHash.h"

using dyld4::PrebuiltObjC;
typedef dyld4::PrebuiltObjC::ObjCOptimizerImage ObjCOptimizerImage;

extern const dyld3::MachOAnalyzer __dso_handle;

@interface PreBuiltObjCTests : XCTestCase
@end


//
// The PreBuiltObjCTester is a utility for wrapping up the KernArgs and delegate needed
// to test the PreBuiltObjC class.
//
class PreBuiltObjCTester
{
    // Mock up enough of a "shared cache" for a selector hash table.  We just need
    // a few strings in a buffer which have 32-bit offsets from the table itself
    struct SelOpt {
        const char _string1[64] = "sharedCacheString1";
        const char _string2[64] = "sharedCacheString2";
        const char _string3[64] = "sharedCacheString3";
        const char _string4[64] = "sharedCacheString4";

        __attribute__((aligned((8))))
        uint8_t    _hashTable[16384] = { };
    };

    // Mock up enough of a "shared cache" for a class hash table.  We just need
    // a few strings in a buffer which have 32-bit offsets from the table itself
    struct ClassOpt {
        __attribute__((aligned((8))))
        mach_header_64  _mh1;
        uint64_t        _class1 = 1;
        uint16_t        _hdrInfoIndex1 = 0;

        __attribute__((aligned((8))))
        mach_header_64  _mh2;
        uint64_t        _class2 = 2;
        uint16_t        _hdrInfoIndex2 = 1;

        __attribute__((aligned((8))))
        mach_header_64  _mh3;
        uint64_t        _class3 = 3;
        uint16_t        _hdrInfoIndex3 = 2;

        __attribute__((aligned((8))))
        mach_header_64  _mh4;
        uint64_t        _class4 = 4;
        uint16_t        _hdrInfoIndex4 = 3;

        __attribute__((aligned((8))))
        uint8_t    _hashTable[16384] = { };

        const char _string1[64] = "sharedCacheString1";
        const char _string2[64] = "sharedCacheString2";
        const char _string3[64] = "sharedCacheString3";
        const char _string4[64] = "sharedCacheString4";
    };

    // Mock up enough of a "shared cache" for a protocol hash table.  We just need
    // a few strings in a buffer which have 32-bit offsets from the table itself
    struct ProtocolOpt {
        mach_header_64  _mh1;
        uint64_t        _protocol1 = 1;
        uint16_t        _hdrInfoIndex1 = 0;

        mach_header_64  _mh2;
        uint64_t        _protocol2 = 2;
        uint16_t        _hdrInfoIndex2 = 1;

        mach_header_64  _mh3;
        uint64_t        _protocol3 = 3;
        uint16_t        _hdrInfoIndex3 = 2;

        mach_header_64  _mh4;
        uint64_t        _protocol4 = 4;
        uint16_t        _hdrInfoIndex4 = 3;

        __attribute__((aligned((8))))
        uint8_t    _hashTable[16384] = { };

        const char _string1[64] = "sharedCacheString1";
        const char _string2[64] = "sharedCacheString2";
        const char _string3[64] = "sharedCacheString3";
        const char _string4[64] = "sharedCacheString4";
    };
public:
    PreBuiltObjCTester(const std::vector<const char*>& envp={});

    // All tests need shared cache hash tables.  Mock up small ones
    objc::SelectorHashTable*                    _objcSelOpt;
    objc::ClassHashTable*                       _objcClassOpt;
    objc::ProtocolHashTable*                    _objcProtocolOpt;

    dyld4::RuntimeState                         _state;

    struct FakeCache {
        uint8_t                                     _fakeDyldCacheBase;
        SelOpt                                      _selOptBuffer;
        ClassOpt                                    _classOptBuffer;
        ClassOpt                                    _protocolOptBuffer;
    };

    FakeCache _fakeCache;

private:
    MockO                                       _mockO;
    dyld4::SyscallDelegate                      _osDelegate;
    dyld4::KernelArgs                           _kernArgs;
    dyld4::ProcessConfig                        _config;
};


// used when the shared cache does not matter.  Just testing the local paths
PreBuiltObjCTester::PreBuiltObjCTester(const std::vector<const char*>& envp)
    : _state(_config), _mockO(MH_EXECUTE, "arm64"), _kernArgs(_mockO.header(), {"test.exe"}, envp, {"executable_path=/foo/test.exe"}),
      _config(&_kernArgs, _osDelegate)
{
    {
        uint64_t selOptVMAddr = 0x100000;
        auto &_selOptBuffer = _fakeCache._selOptBuffer;
        _objcSelOpt = (objc::SelectorHashTable*)_selOptBuffer._hashTable;

        objc::string_map strings;
        strings[_selOptBuffer._string1] = selOptVMAddr + (__offsetof(SelOpt, _string1) - __offsetof(SelOpt, _hashTable));
        strings[_selOptBuffer._string2] = selOptVMAddr + (__offsetof(SelOpt, _string2) - __offsetof(SelOpt, _hashTable));
        strings[_selOptBuffer._string3] = selOptVMAddr + (__offsetof(SelOpt, _string3) - __offsetof(SelOpt, _hashTable));
        strings[_selOptBuffer._string4] = selOptVMAddr + (__offsetof(SelOpt, _string4) - __offsetof(SelOpt, _hashTable));

        Diagnostics diag;
        _objcSelOpt->write(diag, selOptVMAddr, sizeof(_selOptBuffer), strings);
        XCTAssertFalse(diag.hasError());
    }
    {
        uint64_t classOptVMAddr = 0x200000;
        auto &_classOptBuffer = _fakeCache._classOptBuffer;
        _objcClassOpt = (objc::ClassHashTable*)_classOptBuffer._hashTable;

        objc::string_map strings;
        strings[_classOptBuffer._string1] = classOptVMAddr + (__offsetof(ClassOpt, _string1) - __offsetof(ClassOpt, _hashTable));
        strings[_classOptBuffer._string2] = classOptVMAddr + (__offsetof(ClassOpt, _string2) - __offsetof(ClassOpt, _hashTable));
        strings[_classOptBuffer._string3] = classOptVMAddr + (__offsetof(ClassOpt, _string3) - __offsetof(ClassOpt, _hashTable));
        strings[_classOptBuffer._string4] = classOptVMAddr + (__offsetof(ClassOpt, _string4) - __offsetof(ClassOpt, _hashTable));

        // All classes are offsets from the shared cache.  Fake a base address
        uint64_t classOptVMOffset = (__offsetof(FakeCache, _classOptBuffer) - __offsetof(FakeCache, _fakeDyldCacheBase));
        uint64_t sharedCacheBaseVMAddr = classOptVMAddr - classOptVMOffset;

        objc::class_map classes;
        uint64_t classVMAddr1 = sharedCacheBaseVMAddr + (uint64_t)&_classOptBuffer._class1 - (uint64_t)&_fakeCache._fakeDyldCacheBase;
        uint64_t classVMAddr2 = sharedCacheBaseVMAddr + (uint64_t)&_classOptBuffer._class2 - (uint64_t)&_fakeCache._fakeDyldCacheBase;
        uint64_t classVMAddr3 = sharedCacheBaseVMAddr + (uint64_t)&_classOptBuffer._class3 - (uint64_t)&_fakeCache._fakeDyldCacheBase;
        uint64_t classVMAddr4 = sharedCacheBaseVMAddr + (uint64_t)&_classOptBuffer._class4 - (uint64_t)&_fakeCache._fakeDyldCacheBase;
        classes.insert(objc::class_map::value_type(_classOptBuffer._string1, std::pair<uint64_t, uint64_t>(classVMAddr1, _classOptBuffer._hdrInfoIndex1)));
        classes.insert(objc::class_map::value_type(_classOptBuffer._string2, std::pair<uint64_t, uint64_t>(classVMAddr2, _classOptBuffer._hdrInfoIndex2)));
        classes.insert(objc::class_map::value_type(_classOptBuffer._string3, std::pair<uint64_t, uint64_t>(classVMAddr3, _classOptBuffer._hdrInfoIndex3)));
        classes.insert(objc::class_map::value_type(_classOptBuffer._string4, std::pair<uint64_t, uint64_t>(classVMAddr4, _classOptBuffer._hdrInfoIndex4)));

        Diagnostics diag;
        _objcClassOpt->write(diag, classOptVMAddr, sharedCacheBaseVMAddr, sizeof(_classOptBuffer), strings, classes);
        XCTAssertFalse(diag.hasError());
    }
    {
        uint64_t protocolOptVMAddr = 0x300000;
        auto &_protocolOptBuffer = _fakeCache._protocolOptBuffer;
        _objcProtocolOpt = (objc::ProtocolHashTable*)_protocolOptBuffer._hashTable;

        objc::string_map strings;
        strings[_protocolOptBuffer._string1] = protocolOptVMAddr + (__offsetof(ProtocolOpt, _string1) - __offsetof(ProtocolOpt, _hashTable));
        strings[_protocolOptBuffer._string2] = protocolOptVMAddr + (__offsetof(ProtocolOpt, _string2) - __offsetof(ProtocolOpt, _hashTable));
        strings[_protocolOptBuffer._string3] = protocolOptVMAddr + (__offsetof(ProtocolOpt, _string3) - __offsetof(ProtocolOpt, _hashTable));
        strings[_protocolOptBuffer._string4] = protocolOptVMAddr + (__offsetof(ProtocolOpt, _string4) - __offsetof(ProtocolOpt, _hashTable));

        objc::protocol_map protocols;
        uint64_t protocolVMAddr1 = protocolOptVMAddr + (__offsetof(ProtocolOpt, _protocol1) - __offsetof(ProtocolOpt, _hashTable));
        uint64_t protocolVMAddr2 = protocolOptVMAddr + (__offsetof(ProtocolOpt, _protocol2) - __offsetof(ProtocolOpt, _hashTable));
        uint64_t protocolVMAddr3 = protocolOptVMAddr + (__offsetof(ProtocolOpt, _protocol3) - __offsetof(ProtocolOpt, _hashTable));
        uint64_t protocolVMAddr4 = protocolOptVMAddr + (__offsetof(ProtocolOpt, _protocol4) - __offsetof(ProtocolOpt, _hashTable));
        protocols.insert(objc::protocol_map::value_type(_protocolOptBuffer._string1, std::pair<uint64_t, uint64_t>(protocolVMAddr1, _protocolOptBuffer._hdrInfoIndex1)));
        protocols.insert(objc::protocol_map::value_type(_protocolOptBuffer._string2, std::pair<uint64_t, uint64_t>(protocolVMAddr2, _protocolOptBuffer._hdrInfoIndex2)));
        protocols.insert(objc::protocol_map::value_type(_protocolOptBuffer._string3, std::pair<uint64_t, uint64_t>(protocolVMAddr3, _protocolOptBuffer._hdrInfoIndex3)));
        protocols.insert(objc::protocol_map::value_type(_protocolOptBuffer._string4, std::pair<uint64_t, uint64_t>(protocolVMAddr4, _protocolOptBuffer._hdrInfoIndex4)));

        // All classes are offsets from the shared cache.  Fake a base address
        uint64_t protocolOptVMOffset = (__offsetof(FakeCache, _protocolOptBuffer) - __offsetof(FakeCache, _fakeDyldCacheBase));
        uint64_t sharedCacheBaseVMAddr = protocolOptVMAddr - protocolOptVMOffset;

        Diagnostics diag;
        _objcProtocolOpt->write(diag, protocolOptVMAddr, sharedCacheBaseVMAddr, sizeof(_protocolOptBuffer), strings, protocols);
        XCTAssertFalse(diag.hasError());
    }
}


@implementation PreBuiltObjCTests

- (void)setUp {
    // Put setup code here. This method is called before the invocation of each test method in the class.
    self.continueAfterFailure = false;
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
}

- (void)testSelectorHashTableSingleImage
{
    // Arrange: mock up start up config
    PreBuiltObjCTester tester;

    // Note we don't use the mach_header here.  It's just to give us a non-null parameter
    dyld4::JustInTimeLoader* testLoader = dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    testLoader->ref.app = true;
    testLoader->ref.index = 0;

    // Act: add selectors to be optimized
    PrebuiltObjC testPrebuiltObjC;

    uint64_t testImageLoadAddress = 0x10000;
    ObjCOptimizerImage testImage(testLoader, testImageLoadAddress, sizeof(uintptr_t));

    // Add any selector references we need to
    const std::string testString1 = "testString1";
    const uint64_t stringRuntimeOffset = 8;
    const uint64_t selectorReferenceRuntimeOffset = 16;
    testImage.visitReferenceToObjCSelector(tester._objcSelOpt, testPrebuiltObjC.closureSelectorMap, selectorReferenceRuntimeOffset, stringRuntimeOffset, testString1.c_str());
    testPrebuiltObjC.commitImage(testImage);

    // Finish up the hash tables for all images
    testPrebuiltObjC.generateHashTables(tester._state);

    // We should have recorded a single map entry to our selector string
    XCTAssertTrue(testImage.selectorMap.array().count() == 1);
    XCTAssertTrue(testImage.selectorMap.array()[0].first == testString1.c_str());
    XCTAssertTrue(testImage.selectorMap.array()[0].second.loader == testImage.jitLoader);
    XCTAssertTrue(testImage.selectorMap.array()[0].second.runtimeOffset == stringRuntimeOffset);

    // We record all sel ref fixups, even to our own binary
    XCTAssertTrue(testImage.selectorFixups.count() == 1);
    XCTAssertFalse(testImage.selectorFixups[0].isAbsolute());
    XCTAssertTrue(testImage.selectorFixups[0].loaderRef().app == true);
    XCTAssertTrue(testImage.selectorFixups[0].loaderRef().index == 0);
    XCTAssertTrue(testImage.selectorFixups[0].offset() == stringRuntimeOffset);

    // Test the string is in the serialized hash table and points to image 1
    std::optional<dyld4::PrebuiltLoader::BindTargetRef> stringTarget = testPrebuiltObjC.selectorStringTable->getPotentialTarget("testString1");
    XCTAssertTrue(stringTarget.has_value());
    XCTAssertTrue(stringTarget->loaderRef().app == true);
    XCTAssertTrue(stringTarget->loaderRef().index == 0);
    XCTAssertTrue(stringTarget->offset() == stringRuntimeOffset);
}

- (void)testSelectorHashTableMultipleImages
{
    // Arrange: mock up start up config
    PreBuiltObjCTester tester;

    // Note we don't use the mach_header here.  It's just to give us a non-null parameter
    dyld4::JustInTimeLoader* testLoader1 = dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    testLoader1->ref.app = true;
    testLoader1->ref.index = 0;
    dyld4::JustInTimeLoader* testLoader2 = dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    testLoader2->ref.app = true;
    testLoader2->ref.index = 1;

    // Act: add selectors to be optimized
    PrebuiltObjC testPrebuiltObjC;

    uint64_t testImage1LoadAddress = 0x10000;
    ObjCOptimizerImage testImage1(testLoader1, testImage1LoadAddress, sizeof(uintptr_t));

    uint64_t testImage2LoadAddress = 0x20000;
    ObjCOptimizerImage testImage2(testLoader2, testImage2LoadAddress, sizeof(uintptr_t));

    // Add any selector references we need to
    // Image 1 will be our canonical definition of this string
    const std::string testString1 = "testString1";
    const uint64_t stringRuntimeOffset1 = 8;
    const uint64_t selectorReferenceRuntimeOffset1 = 16;
    testImage1.visitReferenceToObjCSelector(tester._objcSelOpt, testPrebuiltObjC.closureSelectorMap, selectorReferenceRuntimeOffset1, stringRuntimeOffset1, testString1.c_str());
    testPrebuiltObjC.commitImage(testImage1);

    // Image 2 has the same string, and should use the definition from image 1
    const std::string testString2 = "testString1";
    const uint64_t stringRuntimeOffset2 = 24;
    const uint64_t selectorReferenceRuntimeOffset2 = 32;
    testImage2.visitReferenceToObjCSelector(tester._objcSelOpt, testPrebuiltObjC.closureSelectorMap, selectorReferenceRuntimeOffset2, stringRuntimeOffset2, testString2.c_str());
    testPrebuiltObjC.commitImage(testImage2);

    // Finish up the hash tables for all images
    testPrebuiltObjC.generateHashTables(tester._state);

    // We should have recorded a single map entry to our selector string
    XCTAssertTrue(testImage1.selectorMap.array().count() == 1);
    XCTAssertTrue(testImage1.selectorMap.array()[0].first == testString1.c_str());
    XCTAssertTrue(testImage1.selectorMap.array()[0].second.loader == testImage1.jitLoader);
    XCTAssertTrue(testImage1.selectorMap.array()[0].second.runtimeOffset == stringRuntimeOffset1);

    // Image 2's maps should be empty as they contain no canonical defintions
    XCTAssertTrue(testImage2.selectorMap.array().empty());

    // We record all sel ref fixups, even to our own binary.  So we should have a fixup in image 1
    XCTAssertTrue(testImage1.selectorFixups.count() == 1);
    XCTAssertFalse(testImage1.selectorFixups[0].isAbsolute());
    XCTAssertTrue(testImage1.selectorFixups[0].loaderRef().app == true);
    XCTAssertTrue(testImage1.selectorFixups[0].loaderRef().index == 0);
    XCTAssertTrue(testImage1.selectorFixups[0].offset() == stringRuntimeOffset1);

    // We should fix up image 2 to point to image 1's definition
    XCTAssertTrue(testImage2.selectorFixups.count() == 1);
    XCTAssertFalse(testImage2.selectorFixups[0].isAbsolute());
    XCTAssertTrue(testImage2.selectorFixups[0].loaderRef().app == true);
    XCTAssertTrue(testImage2.selectorFixups[0].loaderRef().index == 0);
    XCTAssertTrue(testImage2.selectorFixups[0].offset() == stringRuntimeOffset1);

    // Test the string is in the serialized hash table and points to image 1
    std::optional<dyld4::PrebuiltLoader::BindTargetRef> stringTarget = testPrebuiltObjC.selectorStringTable->getPotentialTarget("testString1");
    XCTAssertTrue(stringTarget.has_value());
    XCTAssertTrue(stringTarget->loaderRef().app == true);
    XCTAssertTrue(stringTarget->loaderRef().index == 0);
    XCTAssertTrue(stringTarget->offset() == stringRuntimeOffset1);
}

- (void)testSelectorHashTableSharedCacheSelectors
{
    // Arrange: mock up start up config
    PreBuiltObjCTester tester;

    // Note we don't use the mach_header here.  It's just to give us a non-null parameter
    dyld4::JustInTimeLoader* testLoader1 = dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    testLoader1->ref.app = true;
    testLoader1->ref.index = 0;
    dyld4::JustInTimeLoader* testLoader2 = dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    testLoader2->ref.app = true;
    testLoader2->ref.index = 1;

    // Act: add selectors to be optimized
    PrebuiltObjC testPrebuiltObjC;

    uint64_t testImage1LoadAddress = 0x10000;
    ObjCOptimizerImage testImage1(testLoader1, testImage1LoadAddress, sizeof(uintptr_t));

    uint64_t testImage2LoadAddress = 0x20000;
    ObjCOptimizerImage testImage2(testLoader2, testImage2LoadAddress, sizeof(uintptr_t));

    // Add any selector references we need to
    // Image 1's string should be found in the shared cache
    const std::string testString1 = "sharedCacheString1";
    const uint64_t stringRuntimeOffset1 = 8;
    const uint64_t selectorReferenceRuntimeOffset1 = 16;
    const uint64_t stringVMAddress1 = testImage1LoadAddress + stringRuntimeOffset1;
    const uint64_t selRefVMAddress1 = testImage1LoadAddress + selectorReferenceRuntimeOffset1;
    testImage1.visitReferenceToObjCSelector(tester._objcSelOpt, testPrebuiltObjC.closureSelectorMap, stringVMAddress1, selRefVMAddress1, testString1.c_str());
    testPrebuiltObjC.commitImage(testImage1);

    // Image 2's string should be found in the shared cache
    const std::string testString2 = "sharedCacheString2";
    const uint64_t stringRuntimeOffset2 = 24;
    const uint64_t selectorReferenceRuntimeOffset2 = 32;
    const uint64_t stringVMAddress2 = testImage2LoadAddress + stringRuntimeOffset2;
    const uint64_t selRefVMAddress2 = testImage2LoadAddress + selectorReferenceRuntimeOffset2;
    testImage2.visitReferenceToObjCSelector(tester._objcSelOpt, testPrebuiltObjC.closureSelectorMap, stringVMAddress2, selRefVMAddress2, testString2.c_str());
    testPrebuiltObjC.commitImage(testImage2);

    // Finish up the hash tables for all images
    testPrebuiltObjC.generateHashTables(tester._state);

    // Our maps should be empty, as all selectors come from the shared cache
    XCTAssertTrue(testImage1.selectorMap.array().empty());
    XCTAssertTrue(testImage2.selectorMap.array().empty());

    // Both images should have fixups to point to the shared cache
    // Image 1
    XCTAssertTrue(testImage1.selectorFixups.count() == 1);
    XCTAssertTrue(testImage1.selectorFixups[0].isAbsolute());
    XCTAssertTrue(testImage1.selectorFixups[0].value(tester._state) == *tester._objcSelOpt->tryGetIndex("sharedCacheString1"));
    // Image 2
    XCTAssertTrue(testImage2.selectorFixups.count() == 1);
    XCTAssertTrue(testImage2.selectorFixups[0].isAbsolute());
    XCTAssertTrue(testImage2.selectorFixups[0].value(tester._state) == *tester._objcSelOpt->tryGetIndex("sharedCacheString2"));
}

- (void)testClassHashTableSingleImage
{
    // Arrange: mock up start up config
    PreBuiltObjCTester tester;

    // Note we don't use the mach_header here.  It's just to give us a non-null parameter
    dyld4::JustInTimeLoader* testLoader = dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    testLoader->ref.app = true;
    testLoader->ref.index = 0;

    // Act: add classes to be optimized
    PrebuiltObjC testPrebuiltObjC;
    PrebuiltObjC::SharedCacheImagesMapTy sharedCacheImagesMap;

    uint64_t testImageLoadAddress = 0x10000;
    testPrebuiltObjC.objcImages.emplace_back(testLoader, testImageLoadAddress, (uint32_t)sizeof(uintptr_t));
    ObjCOptimizerImage& testImage = testPrebuiltObjC.objcImages[0];

    // Add any class references we need to
    const std::string testString1 = "testString1";
    const uint64_t classNameRuntimeOffset = 16;
    const uint64_t classValueRuntimeOffset = 24;
    const uint64_t classNameVMAddr = testImageLoadAddress + classNameRuntimeOffset;
    const uint64_t classVMAddr = testImageLoadAddress + classValueRuntimeOffset;
    testImage.visitClass(&tester._fakeCache._fakeDyldCacheBase, tester._objcClassOpt, sharedCacheImagesMap,
                         testPrebuiltObjC.duplicateSharedCacheClassMap, classVMAddr, classNameVMAddr, testString1.c_str());
    testPrebuiltObjC.commitImage(testImage);

    // Finish up the hash tables for all images
    testPrebuiltObjC.generateHashTables(tester._state);

    // We should have recorded a single location entry to our class string
    XCTAssertTrue(testImage.classLocations.count() == 1);
    XCTAssertTrue(testImage.classLocations[0].name == testString1.c_str());
    XCTAssertTrue(testImage.classLocations[0].nameRuntimeOffset == classNameRuntimeOffset);
    XCTAssertTrue(testImage.classLocations[0].valueRuntimeOffset == classValueRuntimeOffset);

    // Test the string is in the serialized hash table and points to image 1
    const dyld4::ObjCClassOpt* appClassOpt = (const dyld4::ObjCClassOpt*)testPrebuiltObjC.classesHashTable.begin();
    __block bool foundTestString1 = false;
    appClassOpt->forEachClass(tester._state, ^(const dyld4::PrebuiltLoader::BindTargetRef &nameTarget, const Array<dyld4::PrebuiltLoader::BindTargetRef> &implTargets) {
        if ( (nameTarget.loaderRef().app == 1) && (nameTarget.loaderRef().index == 0) && (nameTarget.offset() == classNameRuntimeOffset) ) {
            foundTestString1 = true;

            XCTAssertTrue(implTargets.count() == 1);
            XCTAssertTrue(implTargets[0].loaderRef().app == 1);
            XCTAssertTrue(implTargets[0].loaderRef().index == 0);
            XCTAssertTrue(implTargets[0].offset() == classValueRuntimeOffset);
        }
    });
    XCTAssertTrue(foundTestString1);
}

- (void)testClassHashTableSharedCacheSelectors
{
    // Arrange: mock up start up config
    PreBuiltObjCTester tester;

    // Note we don't use the mach_header here.  It's just to give us a non-null parameter
    dyld4::JustInTimeLoader* testLoader = dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    testLoader->ref.app = true;
    testLoader->ref.index = 0;

    // Act: add classes to be optimized
    PrebuiltObjC testPrebuiltObjC;

    // Fake some shared cache images
    PrebuiltObjC::SharedCacheImagesMapTy sharedCacheImagesMap;
    dyld4::Loader* fakeCacheLoader1 = (dyld4::Loader*)dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    fakeCacheLoader1->ref.app = false;
    fakeCacheLoader1->ref.index = 0;
    dyld4::Loader* fakeCacheLoader2 = (dyld4::Loader*)dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    fakeCacheLoader2->ref.app = false;
    fakeCacheLoader2->ref.index = 1;
    dyld4::Loader* fakeCacheLoader3 = (dyld4::Loader*)dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    fakeCacheLoader3->ref.app = false;
    fakeCacheLoader3->ref.index = 2;
    dyld4::Loader* fakeCacheLoader4 = (dyld4::Loader*)dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    fakeCacheLoader4->ref.app = false;
    fakeCacheLoader4->ref.index = 3;

    // Note the mach_header's here need to be those in the fake cache, as we have offsets from these mach-headers to class objects
    sharedCacheImagesMap[0] = { (const dyld3::MachOAnalyzer*)&tester._fakeCache._classOptBuffer._mh1, fakeCacheLoader1 };
    sharedCacheImagesMap[1] = { (const dyld3::MachOAnalyzer*)&tester._fakeCache._classOptBuffer._mh2, fakeCacheLoader2 };
    sharedCacheImagesMap[2] = { (const dyld3::MachOAnalyzer*)&tester._fakeCache._classOptBuffer._mh3, fakeCacheLoader3 };
    sharedCacheImagesMap[3] = { (const dyld3::MachOAnalyzer*)&tester._fakeCache._classOptBuffer._mh4, fakeCacheLoader4 };

    uint64_t testImageLoadAddress = 0x10000;
    testPrebuiltObjC.objcImages.emplace_back(testLoader, testImageLoadAddress, (uint32_t)sizeof(uintptr_t));
    ObjCOptimizerImage& testImage = testPrebuiltObjC.objcImages[0];

    // Add any class references we need to
    const std::string testString1 = "sharedCacheString3";
    const uint64_t classNameRuntimeOffset = 16;
    const uint64_t classValueRuntimeOffset = 24;
    const uint64_t classNameVMAddr = testImageLoadAddress + classNameRuntimeOffset;
    const uint64_t classVMAddr = testImageLoadAddress + classValueRuntimeOffset;
    testImage.visitClass(&tester._fakeCache._fakeDyldCacheBase, tester._objcClassOpt, sharedCacheImagesMap,
                         testPrebuiltObjC.duplicateSharedCacheClassMap, classVMAddr, classNameVMAddr, testString1.c_str());
    testPrebuiltObjC.commitImage(testImage);

    // Finish up the hash tables for all images
    testPrebuiltObjC.generateHashTables(tester._state);

    // We should have recorded a duplicate class entry
    // This just happens to be the offset in the _classOptBuffer above.
    const uint64_t sharedCacheClassImplOffset = 0x20;
    XCTAssertTrue(testImage.duplicateSharedCacheClassMap.array().count() == 1);
    XCTAssertTrue(testImage.duplicateSharedCacheClassMap.array()[0].first == testString1.c_str());
    XCTAssertTrue(testImage.duplicateSharedCacheClassMap.array()[0].second.loader == fakeCacheLoader3);
    XCTAssertTrue(testImage.duplicateSharedCacheClassMap.array()[0].second.runtimeOffset == sharedCacheClassImplOffset);

    // We should have recorded a single location entry to our class string
    XCTAssertTrue(testImage.classLocations.count() == 1);
    XCTAssertTrue(testImage.classLocations[0].name == testString1.c_str());
    XCTAssertTrue(testImage.classLocations[0].nameRuntimeOffset == classNameRuntimeOffset);
    XCTAssertTrue(testImage.classLocations[0].valueRuntimeOffset == classValueRuntimeOffset);

    // Test the string is in the serialized hash table and points to image 1
    const dyld4::ObjCClassOpt* appClassOpt = (const dyld4::ObjCClassOpt*)testPrebuiltObjC.classesHashTable.begin();
    __block bool foundTestString1 = false;
    appClassOpt->forEachClass(tester._state, ^(const dyld4::PrebuiltLoader::BindTargetRef &nameTarget, const Array<dyld4::PrebuiltLoader::BindTargetRef> &implTargets) {
        if ( (nameTarget.loaderRef().app == 1) && (nameTarget.loaderRef().index == 0) && (nameTarget.offset() == classNameRuntimeOffset) ) {
            foundTestString1 = true;

            XCTAssertTrue(implTargets.count() == 2);
            // The first impl should be the duplicate we found in shared cache image 3
            XCTAssertTrue(implTargets[0].loaderRef().app == 0);
            XCTAssertTrue(implTargets[0].loaderRef().index == 2);
            XCTAssertTrue(implTargets[0].offset() == sharedCacheClassImplOffset);
            // Then the implementation in the app image
            XCTAssertTrue(implTargets[1].loaderRef().app == 1);
            XCTAssertTrue(implTargets[1].loaderRef().index == 0);
            XCTAssertTrue(implTargets[1].offset() == classValueRuntimeOffset);
        }
    });
    XCTAssertTrue(foundTestString1);
}

// This is the same as testClassHashTableSharedCacheSelectors, except we have more than 1 duplicate of the shared cache class
- (void)testClassHashTableSharedCacheSelectorsMultipleDuplicates
{
    // Arrange: mock up start up config
    PreBuiltObjCTester tester;

    // Note we don't use the mach_header here.  It's just to give us a non-null parameter
    dyld4::JustInTimeLoader* testLoader1 = dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    testLoader1->ref.app = true;
    testLoader1->ref.index = 0;

    dyld4::JustInTimeLoader* testLoader2 = dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    testLoader2->ref.app = true;
    testLoader2->ref.index = 1;

    // Act: add classes to be optimized
    PrebuiltObjC testPrebuiltObjC;

    // Fake some shared cache images
    PrebuiltObjC::SharedCacheImagesMapTy sharedCacheImagesMap;
    dyld4::Loader* fakeCacheLoader1 = (dyld4::Loader*)dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    fakeCacheLoader1->ref.app = false;
    fakeCacheLoader1->ref.index = 0;
    dyld4::Loader* fakeCacheLoader2 = (dyld4::Loader*)dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    fakeCacheLoader2->ref.app = false;
    fakeCacheLoader2->ref.index = 1;
    dyld4::Loader* fakeCacheLoader3 = (dyld4::Loader*)dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    fakeCacheLoader3->ref.app = false;
    fakeCacheLoader3->ref.index = 2;
    dyld4::Loader* fakeCacheLoader4 = (dyld4::Loader*)dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    fakeCacheLoader4->ref.app = false;
    fakeCacheLoader4->ref.index = 3;

    // Note the mach_header's here need to be those in the fake cache, as we have offsets from these mach-headers to class objects
    sharedCacheImagesMap[0] = { (const dyld3::MachOAnalyzer*)&tester._fakeCache._classOptBuffer._mh1, fakeCacheLoader1 };
    sharedCacheImagesMap[1] = { (const dyld3::MachOAnalyzer*)&tester._fakeCache._classOptBuffer._mh2, fakeCacheLoader2 };
    sharedCacheImagesMap[2] = { (const dyld3::MachOAnalyzer*)&tester._fakeCache._classOptBuffer._mh3, fakeCacheLoader3 };
    sharedCacheImagesMap[3] = { (const dyld3::MachOAnalyzer*)&tester._fakeCache._classOptBuffer._mh4, fakeCacheLoader4 };

    uint64_t testImageLoadAddress1 = 0x10000;
    testPrebuiltObjC.objcImages.emplace_back(testLoader1, testImageLoadAddress1, (uint32_t)sizeof(uintptr_t));
    uint64_t testImageLoadAddress2 = 0x20000;
    testPrebuiltObjC.objcImages.emplace_back(testLoader2, testImageLoadAddress2, (uint32_t)sizeof(uintptr_t));
    ObjCOptimizerImage& testImage1 = testPrebuiltObjC.objcImages[0];
    ObjCOptimizerImage& testImage2 = testPrebuiltObjC.objcImages[1];

    // Add any class references we need to
    const std::string testString = "sharedCacheString3";
    const uint64_t classNameRuntimeOffset = 16;
    const uint64_t classValueRuntimeOffset = 24;

    // Image 1
    const uint64_t classNameVMAddr1 = testImageLoadAddress1 + classNameRuntimeOffset;
    const uint64_t classVMAddr1 = testImageLoadAddress1 + classValueRuntimeOffset;
    testImage1.visitClass(&tester._fakeCache._fakeDyldCacheBase, tester._objcClassOpt, sharedCacheImagesMap,
                          testPrebuiltObjC.duplicateSharedCacheClassMap, classVMAddr1, classNameVMAddr1, testString.c_str());
    testPrebuiltObjC.commitImage(testImage1);

    // Image 2
    const uint64_t classNameVMAddr2 = testImageLoadAddress2 + classNameRuntimeOffset;
    const uint64_t classVMAddr2 = testImageLoadAddress2 + classValueRuntimeOffset;
    testImage2.visitClass(&tester._fakeCache._fakeDyldCacheBase, tester._objcClassOpt, sharedCacheImagesMap,
                          testPrebuiltObjC.duplicateSharedCacheClassMap, classVMAddr2, classNameVMAddr2, testString.c_str());
    testPrebuiltObjC.commitImage(testImage2);

    // Finish up the hash tables for all images
    testPrebuiltObjC.generateHashTables(tester._state);

    // We should have recorded a duplicate class entry
    // This just happens to be the offset in the _classOptBuffer above.
    const uint64_t sharedCacheClassImplOffset = 0x20;

    // Image 1
    XCTAssertTrue(testImage1.duplicateSharedCacheClassMap.array().count() == 1);
    XCTAssertTrue(testImage1.duplicateSharedCacheClassMap.array()[0].first == testString.c_str());
    XCTAssertTrue(testImage1.duplicateSharedCacheClassMap.array()[0].second.loader == fakeCacheLoader3);
    XCTAssertTrue(testImage1.duplicateSharedCacheClassMap.array()[0].second.runtimeOffset == sharedCacheClassImplOffset);
    // We should have recorded a single location entry to our class string
    XCTAssertTrue(testImage1.classLocations.count() == 1);
    XCTAssertTrue(testImage1.classLocations[0].name == testString.c_str());
    XCTAssertTrue(testImage1.classLocations[0].nameRuntimeOffset == classNameRuntimeOffset);
    XCTAssertTrue(testImage1.classLocations[0].valueRuntimeOffset == classValueRuntimeOffset);

    // Image 2
    XCTAssertTrue(testImage2.duplicateSharedCacheClassMap.array().empty());
    // We should have recorded a single location entry to our class string
    XCTAssertTrue(testImage2.classLocations.count() == 1);
    XCTAssertTrue(testImage2.classLocations[0].name == testString.c_str());
    XCTAssertTrue(testImage2.classLocations[0].nameRuntimeOffset == classNameRuntimeOffset);
    XCTAssertTrue(testImage2.classLocations[0].valueRuntimeOffset == classValueRuntimeOffset);

    // Test the string is in the serialized hash table and points to image 1
    const dyld4::ObjCClassOpt* appClassOpt = (const dyld4::ObjCClassOpt*)testPrebuiltObjC.classesHashTable.begin();
    __block bool foundTestString1 = false;
    appClassOpt->forEachClass(tester._state, ^(const dyld4::PrebuiltLoader::BindTargetRef &nameTarget, const Array<dyld4::PrebuiltLoader::BindTargetRef> &implTargets) {
        if ( (nameTarget.loaderRef().app == 1) && (nameTarget.loaderRef().index == 1) && (nameTarget.offset() == classNameRuntimeOffset) ) {
            foundTestString1 = true;

            XCTAssertTrue(implTargets.count() == 3);
            // The first impl should be the duplicate we found in shared cache image 3
            XCTAssertTrue(implTargets[0].loaderRef().app == 0);
            XCTAssertTrue(implTargets[0].loaderRef().index == 2);
            XCTAssertTrue(implTargets[0].offset() == sharedCacheClassImplOffset);
            // Then the implementation in the second test image
            XCTAssertTrue(implTargets[1].loaderRef().app == true);
            XCTAssertTrue(implTargets[1].loaderRef().index == testLoader2->ref.index);
            XCTAssertTrue(implTargets[1].offset() == classValueRuntimeOffset);
            // Then the implementation in the first test image
            XCTAssertTrue(implTargets[2].loaderRef().app == 1);
            XCTAssertTrue(implTargets[2].loaderRef().index == testLoader1->ref.index);
            XCTAssertTrue(implTargets[2].offset() == classValueRuntimeOffset);
        }
    });
    XCTAssertTrue(foundTestString1);
}

- (void)testProtocolHashTableSingleImage
{
    // Arrange: mock up start up config
    PreBuiltObjCTester tester;

    // Note we don't use the mach_header here.  It's just to give us a non-null parameter
    dyld4::JustInTimeLoader* testLoader = dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    testLoader->ref.app = true;
    testLoader->ref.index = 0;

    // Act: add protocols to be optimized
    PrebuiltObjC testPrebuiltObjC;
    PrebuiltObjC::SharedCacheImagesMapTy sharedCacheImagesMap;

    uint64_t testImageLoadAddress = 0x10000;
    testPrebuiltObjC.objcImages.emplace_back(testLoader, testImageLoadAddress, (uint32_t)sizeof(uintptr_t));
    ObjCOptimizerImage& testImage = testPrebuiltObjC.objcImages[0];

    // Add any protocol references we need to
    const std::string testString1 = "testString1";
    const uint64_t protocolNameRuntimeOffset = 16;
    const uint64_t protocolValueRuntimeOffset = 24;
    const uint64_t protocolNameVMAddr = testImageLoadAddress + protocolNameRuntimeOffset;
    const uint64_t protocolVMAddr = testImageLoadAddress + protocolValueRuntimeOffset;
    testImage.visitProtocol(tester._objcProtocolOpt, sharedCacheImagesMap, protocolVMAddr, protocolNameVMAddr, testString1.c_str());
    testPrebuiltObjC.commitImage(testImage);

    // Finish up the hash tables for all images
    testPrebuiltObjC.generateHashTables(tester._state);

    // We should have recorded a single location entry to our protocol string
    XCTAssertTrue(testImage.protocolLocations.count() == 1);
    XCTAssertTrue(testImage.protocolLocations[0].name == testString1.c_str());
    XCTAssertTrue(testImage.protocolLocations[0].nameRuntimeOffset == protocolNameRuntimeOffset);
    XCTAssertTrue(testImage.protocolLocations[0].valueRuntimeOffset == protocolValueRuntimeOffset);

    // Test the string is in the serialized hash table and points to image 1
    const dyld4::ObjCClassOpt* appProtocolOpt = (const dyld4::ObjCClassOpt*)testPrebuiltObjC.protocolsHashTable.begin();
    __block bool foundTestString1 = false;
    appProtocolOpt->forEachClass(tester._state, ^(const dyld4::PrebuiltLoader::BindTargetRef &nameTarget, const Array<dyld4::PrebuiltLoader::BindTargetRef> &implTargets) {
        if ( (nameTarget.loaderRef().app == 1) && (nameTarget.loaderRef().index == 0) && (nameTarget.offset() == protocolNameRuntimeOffset) ) {
            foundTestString1 = true;

            XCTAssertTrue(implTargets.count() == 1);
            XCTAssertTrue(implTargets[0].loaderRef().app == 1);
            XCTAssertTrue(implTargets[0].loaderRef().index == 0);
            XCTAssertTrue(implTargets[0].offset() == protocolValueRuntimeOffset);
        }
    });
    XCTAssertTrue(foundTestString1);
}

- (void)testProtocolHashTableSharedCacheSelectors
{
    // Arrange: mock up start up config
    PreBuiltObjCTester tester;

    // Note we don't use the mach_header here.  It's just to give us a non-null parameter
    dyld4::JustInTimeLoader* testLoader = dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    testLoader->ref.app = true;
    testLoader->ref.index = 0;

    // Act: add protocols to be optimized
    PrebuiltObjC testPrebuiltObjC;

    // Fake some shared cache images
    PrebuiltObjC::SharedCacheImagesMapTy sharedCacheImagesMap;
    dyld4::Loader* fakeCacheLoader1 = (dyld4::Loader*)dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    fakeCacheLoader1->ref.app = false;
    fakeCacheLoader1->ref.index = 0;
    dyld4::Loader* fakeCacheLoader2 = (dyld4::Loader*)dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    fakeCacheLoader2->ref.app = false;
    fakeCacheLoader2->ref.index = 1;
    dyld4::Loader* fakeCacheLoader3 = (dyld4::Loader*)dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    fakeCacheLoader3->ref.app = false;
    fakeCacheLoader3->ref.index = 2;
    dyld4::Loader* fakeCacheLoader4 = (dyld4::Loader*)dyld4::JustInTimeLoader::makeJustInTimeLoader(tester._state, &__dso_handle, "");
    fakeCacheLoader4->ref.app = false;
    fakeCacheLoader4->ref.index = 3;

    sharedCacheImagesMap[0] = { &__dso_handle, fakeCacheLoader1 };
    sharedCacheImagesMap[1] = { &__dso_handle, fakeCacheLoader2 };
    sharedCacheImagesMap[2] = { &__dso_handle, fakeCacheLoader3 };
    sharedCacheImagesMap[3] = { &__dso_handle, fakeCacheLoader4 };

    uint64_t testImageLoadAddress = 0x10000;
    testPrebuiltObjC.objcImages.emplace_back(testLoader, testImageLoadAddress, (uint32_t)sizeof(uintptr_t));
    ObjCOptimizerImage& testImage = testPrebuiltObjC.objcImages[0];

    // Add any protocol references we need to
    const std::string testString1 = "sharedCacheString3";
    const uint64_t protocolNameRuntimeOffset = 16;
    const uint64_t protocolValueRuntimeOffset = 24;
    const uint64_t protocolNameVMAddr = testImageLoadAddress + protocolNameRuntimeOffset;
    const uint64_t protocolVMAddr = testImageLoadAddress + protocolValueRuntimeOffset;
    testImage.visitProtocol(tester._objcProtocolOpt, sharedCacheImagesMap, protocolVMAddr, protocolNameVMAddr, testString1.c_str());
    testPrebuiltObjC.commitImage(testImage);

    // Finish up the hash tables for all images
    testPrebuiltObjC.generateHashTables(tester._state);

    // We should have no protocols to optimize here as the shared cache version wins
    XCTAssertTrue(testImage.protocolLocations.empty());

    // The protocol table should be empty as we had nothing to serialize
    XCTAssertTrue(testPrebuiltObjC.protocolsHashTable.empty());
}


@end
