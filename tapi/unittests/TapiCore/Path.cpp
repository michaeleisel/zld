//===- unittests/Path/Path.cpp - Path Tests -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "tapi/Core/Path.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "gtest/gtest.h"
#define DEBUG_TYPE "path-test"

using namespace llvm;
using namespace tapi::internal;

TEST(Path, replace_extension) {
  SmallString<PATH_MAX> src;
  const char *dst = nullptr;
  src = "/some/path/file";
  dst = "/some/path/file.tbd";
  replace_extension(src, ".tbd");
  EXPECT_STREQ(src.c_str(), dst);

  src = "/some/path/file.foo.dylib";
  dst = "/some/path/file.foo.tbd";
  replace_extension(src, ".tbd");
  EXPECT_STREQ(src.c_str(), dst);

  src = "/some/path/Foo.framework/Foo";
  dst = "/some/path/Foo.framework/Foo.tbd";
  replace_extension(src, ".tbd");
  EXPECT_STREQ(src.c_str(), dst);

  src = "/some/path/Foo.extension.framework/Foo.extension";
  dst = "/some/path/Foo.extension.framework/Foo.extension.tbd";
  replace_extension(src, ".tbd");
  EXPECT_STREQ(src.c_str(), dst);

  src = "/some/path/Foo.extension.framework/Bar";
  dst = "/some/path/Foo.extension.framework/Bar.tbd";
  replace_extension(src, ".tbd");
  EXPECT_STREQ(src.c_str(), dst);

  src = "/some/path/Foo.extension.framework/Bar.extension";
  dst = "/some/path/Foo.extension.framework/Bar.tbd";
  replace_extension(src, ".tbd");
  EXPECT_STREQ(src.c_str(), dst);
}
