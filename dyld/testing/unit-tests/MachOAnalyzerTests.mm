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


@interface MachOAnalyzerTests : XCTestCase
@end

@implementation MachOAnalyzerTests


- (void)test_symbolTable
{
    // Arrange: make mach-o file with symbols
    MockO mock(MH_EXECUTE, "arm64");
    mock.customizeAddFunction("_foo", true);
    mock.customizeAddFunction("_bar", true);
    mock.customizeAddFunction("_static1", false);
    mock.customizeAddFunction("_static2", false);
    char xx[1024];
    mock.save(xx);

    // Act: test symbol table
    const MachOAnalyzer* ma = mock.header();
    __block bool foundFoo         = false;
    __block bool foundBar         = false;
    __block bool foundExtraGlobal = false;
    Diagnostics diag;
    ma->forEachGlobalSymbol(diag, ^(const char* symbolName, uint64_t n_value, uint8_t n_type, uint8_t n_sect, uint16_t n_desc, bool& stop) {
        if ( strcmp(symbolName, "_foo") == 0 )
            foundFoo = true;
        else if ( strcmp(symbolName, "_bar") == 0 )
            foundBar = true;
        else
            foundExtraGlobal = true;
    });
    __block bool foundStatic1    = false;
    __block bool foundStatic2    = false;
    __block bool foundExtraLocal = false;
    ma->forEachLocalSymbol(diag, ^(const char* symbolName, uint64_t n_value, uint8_t n_type, uint8_t n_sect, uint16_t n_desc, bool& stop) {
        if ( strcmp(symbolName, "_static1") == 0 )
            foundStatic1 = true;
        else if ( strcmp(symbolName, "_static2") == 0 )
            foundStatic2 = true;
        else
            foundExtraLocal = true;
    });
    Diagnostics validationDiag;
    bool validMockO = mock.header()->isValidMainExecutable(validationDiag, "/foo", mock.size(), GradedArchs::arm64, Platform::macOS);

    // Assert: symbols are as expected
    XCTAssertTrue(validMockO);
    XCTAssertFalse(validationDiag.hasError());
    XCTAssertFalse(diag.hasError());
    XCTAssertTrue(foundFoo);
    XCTAssertTrue(foundBar);
    XCTAssertFalse(foundExtraGlobal);
    XCTAssertTrue(foundStatic1);
    XCTAssertTrue(foundStatic2);
    XCTAssertFalse(foundExtraLocal);
}

- (void)test_forEachExportedSymbol_chainedFixups
{
    // Arrange: make mach-o file with symbols
    MockO mock(MH_EXECUTE, "arm64", Platform::iOS, "15.0");
    mock.customizeAddFunction("_foo", true);
    mock.customizeAddFunction("_bar", true);
    mock.customizeAddFunction("_static1", false);
    mock.customizeAddFunction("_static2", false);

    // Act: test symbol table
    const MachOAnalyzer* ma = mock.header();
    __block bool foundFoo         = false;
    __block bool foundBar         = false;
    __block bool foundExtraGlobal = false;
    Diagnostics diag;
    ma->forEachExportedSymbol(diag, ^(const char* symbolName, uint64_t imageOffset, uint64_t flags, uint64_t other, const char* importName, bool& stop) {
        if ( strcmp(symbolName, "_foo") == 0 )
            foundFoo = true;
        else if ( strcmp(symbolName, "_bar") == 0 )
            foundBar = true;
        else
            foundExtraGlobal = true;
    });

    // Assert: symbols are as expected
    XCTAssertFalse(diag.hasError());
    XCTAssertTrue(foundFoo);
    XCTAssertTrue(foundBar);
    XCTAssertFalse(foundExtraGlobal);
}

- (void)test_forEachExportedSymbol_dyldinfo
{
    // Arrange: make mach-o file with symbols
    MockO mock(MH_EXECUTE, "arm64", Platform::iOS, "12.0");
   mock.customizeAddFunction("_foo", true);
    mock.customizeAddFunction("_bar", true);
    mock.customizeAddFunction("_static1", false);
    mock.customizeAddFunction("_static2", false);

    // Act: test symbol table
    const MachOAnalyzer* ma = mock.header();
    __block bool foundFoo         = false;
    __block bool foundBar         = false;
    __block bool foundExtraGlobal = false;
    Diagnostics diag;
    ma->forEachExportedSymbol(diag, ^(const char* symbolName, uint64_t imageOffset, uint64_t flags, uint64_t other, const char* importName, bool& stop) {
        if ( strcmp(symbolName, "_foo") == 0 )
            foundFoo = true;
        else if ( strcmp(symbolName, "_bar") == 0 )
            foundBar = true;
        else
            foundExtraGlobal = true;
    });

    // Assert: symbols are as expected
    XCTAssertFalse(diag.hasError());
    XCTAssertTrue(foundFoo);
    XCTAssertTrue(foundBar);
    XCTAssertFalse(foundExtraGlobal);
}




@end
