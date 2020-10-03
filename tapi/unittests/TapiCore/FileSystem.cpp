//===- unittests/FileSystem/FileSystem.cpp - FileSytem Test ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "tapi/Core/FileSystem.h"
#include "llvm/ADT/SmallString.h"
#include "gtest/gtest.h"
#define DEBUG_TYPE "filesystem-test"

using namespace llvm;
using namespace tapi::internal;

TEST(FileSystem, make_relative) {
  StringRef src = "/some/path/file1";
  StringRef dst = "/some/path/file2";
  SmallString<PATH_MAX> result;
  make_relative(src, dst, result);
  EXPECT_STREQ(result.c_str(), "file2");

  src = "/some/path1/path1";
  dst = "/some/path2/path2";
  make_relative(src, dst, result);
  EXPECT_STREQ(result.c_str(), "../path2/path2");

  src = "/some/path1/path1";
  dst = "/some/path2";
  make_relative(src, dst, result);
  EXPECT_STREQ(result.c_str(), "../path2");

  src = "/path1/bar";
  dst = "/path2/foo";
  make_relative(src, dst, result);
  EXPECT_STREQ(result.c_str(), "../path2/foo");

  src = "/some/path1/path1";
  dst = "/some/path1";
  make_relative(src, dst, result);
  EXPECT_STREQ(result.c_str(), ".");

  src = "/some/path1/foo";
  dst = "/some/path1/path2/bar";
  make_relative(src, dst, result);
  EXPECT_STREQ(result.c_str(), "path2/bar");
}
