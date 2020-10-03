//===- unittests/JSONSerializer/JSONSerializer.cpp - JSON Serializer Test -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "tapi/Core/APIJSONSerializer.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#define DEBUG_TYPE "json-test"

using namespace llvm;
using namespace tapi::internal;

namespace {

// Round trip serialization test.
// Make sure the input and output are exactly the same.
TEST(JSON, Serializer) {
  SmallVector<char, PATH_MAX> inputPath;
  llvm::sys::path::append(inputPath, INPUT_PATH, "binary.json");
  auto inputBuf = MemoryBuffer::getFile(inputPath);
  EXPECT_TRUE(inputBuf);

  auto api = APIJSONSerializer::parse((*inputBuf)->getBuffer());
  EXPECT_FALSE(!api);

  std::string output;
  raw_string_ostream os(output);
  APIJSONSerializer serializer(*api);
  serializer.serialize(os);

  EXPECT_STREQ((*inputBuf)->getBuffer().str().c_str(), os.str().c_str());
}

TEST(JSON, Serializer2) {
  SmallVector<char, PATH_MAX> inputPath;
  llvm::sys::path::append(inputPath, INPUT_PATH, "frontend.json");
  auto inputBuf = MemoryBuffer::getFile(inputPath);
  EXPECT_TRUE(inputBuf);

  auto api = APIJSONSerializer::parse((*inputBuf)->getBuffer());
  EXPECT_FALSE(!api);

  std::string output;
  raw_string_ostream os(output);
  APIJSONSerializer serializer(*api);
  serializer.serialize(os);

  EXPECT_STREQ((*inputBuf)->getBuffer().str().c_str(), os.str().c_str());
}

} // end anonymous namespace.
