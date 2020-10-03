//===- unittests/Utils/Utils.cpp - Utils Test -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "tapi/Core/Utils.h"
#include "llvm/ADT/SmallString.h"
#include "gtest/gtest.h"
#define DEBUG_TYPE "utils-test"

using namespace llvm;
using namespace tapi::internal;

TEST(Utils, isPublicLocation) {
  static const char *publicPaths[] = {
      "/usr/lib/libfoo.dylib",
      "/usr/lib/swift/libswiftCore.dylib",
      "/usr/lib/swift/subfolder/libswift_internal.dylib",
      "/System/Library/Frameworks/Foo.framework/Foo",
      "/System/Library/Frameworks/Foo.framework/Versions/A/Foo",
      "/System/Library/Frameworks/Public.framework/Versions/A/Public",
      "/System/iOSSupport/usr/lib/libfoo.dylib",
      "/System/iOSSupport/System/Library/Frameworks/Foo.framework/Foo",
      "/System/iOSSupport/System/Library/Frameworks/Foo.framework/Versions/A/"
      "Foo",
      "/System/iOSSupport/System/Library/Frameworks/Public.framework/Versions/"
      "A/Public",
      "/System/DriverKit/usr/lib/libfoo.dylib",
  };

  static const char *privatePaths[] = {
      "/usr/lib/system/libsystem_foo.dylib",
      "/System/Library/Frameworks/Foo.framework/Resources/libBar.dylib",
      "/System/Library/Frameworks/Foo.framework/Frameworks/Bar.framework/Bar",
      "/System/Library/Frameworks/Foo.framework/Frameworks/XFoo.framework/XFoo",
      "/System/iOSSupport/usr/lib/system/libsystem_foo.dylib",
      "/System/iOSSupport/System/Library/Frameworks/Foo.framework/Resources/"
      "libBar.dylib",
      "/System/iOSSupport/System/Library/Frameworks/Foo.framework/Frameworks/"
      "Bar.framework/Bar",
      "/System/iOSSupport/System/Library/Frameworks/Foo.framework/Frameworks/"
      "XFoo.framework/XFoo",
      "/System/DriverKit/usr/local/lib/libfoo.dylib",
  };

  for (const char *path : publicPaths)
    EXPECT_TRUE(isPublicLocation(path));

  for (const char *path : privatePaths)
    EXPECT_FALSE(isPublicLocation(path));
}
