//===- unittests/TapiVerifier/IO.cpp - Verifier IO Tests ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "tapi/APIVerifier/APIVerifier.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Path.h"
#include "gtest/gtest.h"
#define DEBUG_TYPE "verifier-io-test"

using namespace llvm;
using namespace tapi::internal;

TEST(Verifier, APIConfigReader) {
  APIVerifierConfiguration config;

  SmallVector<char, PATH_MAX> inputPath;
  llvm::sys::path::append(inputPath, INPUT_PATH, "test.yaml");
  auto inputBuf = MemoryBuffer::getFile(inputPath);
  EXPECT_TRUE(inputBuf);

  auto error = config.readConfig((*inputBuf)->getMemBufferRef());
  EXPECT_FALSE(error);

  EXPECT_STREQ(config.IgnoreObjCClasses[0].c_str(), "MyClass");
  EXPECT_STREQ(config.IgnoreObjCClasses[1].c_str(), "MyClass2");
  EXPECT_STREQ(config.BridgeObjCClasses[0].first.c_str(), "NSColor");
  EXPECT_STREQ(config.BridgeObjCClasses[0].second.c_str(), "UIColor");
}

TEST(Verifer, APIConfigWriter) {
  APIVerifierConfiguration config;
  config.IgnoreObjCClasses.emplace_back("MyClass");
  config.IgnoreObjCClasses.emplace_back("MyClass2");
  config.BridgeObjCClasses.emplace_back("NSColor", "UIColor");

  std::string outString;
  llvm::raw_string_ostream os(outString);
  config.writeConfig(os);
  os.flush();

  SmallVector<char, PATH_MAX> inputPath;
  llvm::sys::path::append(inputPath, INPUT_PATH, "test.yaml");
  auto inputBuf = MemoryBuffer::getFile(inputPath);
  EXPECT_TRUE(inputBuf);
  EXPECT_STREQ((*inputBuf)->getBuffer().str().c_str(), outString.c_str());
}

TEST(Verifer, FailedRead) {
  APIVerifierConfiguration config;

  const char *input = "bogus input\n";
  auto inputBuf = MemoryBuffer::getMemBuffer(input);

  auto error = config.readConfig(inputBuf->getMemBufferRef());
  EXPECT_FALSE(!error);

  auto errorMsg = toString(std::move(error));

  EXPECT_STREQ(errorMsg.c_str(), "Invalid argument");
}
