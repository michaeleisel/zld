//===- unittests/TapiDriver/HeaderGlob.cpp - Glob Tests -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "tapi/Driver/HeaderGlob.h"
#include "gtest/gtest.h"
#define DEBUG_TYPE "glob-tests"

using namespace llvm;
using namespace tapi::internal;

TEST(HeaderGlob, matchWildcard) {
  auto glob = HeaderGlob::create("*.h", HeaderType::Public);
  EXPECT_TRUE(!!glob);
  EXPECT_TRUE(glob.get()->match({"foo.h", HeaderType::Public}));
  EXPECT_TRUE(glob.get()->match({"bar.h", HeaderType::Public}));
  EXPECT_FALSE(glob.get()->match({"foo.h", HeaderType::Private}));
  EXPECT_FALSE(glob.get()->match({"bar.hpp", HeaderType::Public}));
  EXPECT_FALSE(glob.get()->match({"bar.c", HeaderType::Public}));
  EXPECT_FALSE(glob.get()->match({"/baz/bar.h", HeaderType::Public}));
}

TEST(HeaderGlob, matchGlob) {
  auto glob = HeaderGlob::create("**/*.h", HeaderType::Public);
  EXPECT_TRUE(!!glob);
  EXPECT_TRUE(glob.get()->match({"/foo.h", HeaderType::Public}));
  EXPECT_TRUE(glob.get()->match({"/bar.h", HeaderType::Public}));
  EXPECT_TRUE(glob.get()->match({"/baz/bar.h", HeaderType::Public}));
  EXPECT_FALSE(glob.get()->match({"/bar.c", HeaderType::Public}));
  EXPECT_FALSE(glob.get()->match({"/baz/bar.hpp", HeaderType::Public}));
}
