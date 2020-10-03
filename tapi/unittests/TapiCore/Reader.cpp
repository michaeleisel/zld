//===- unittests/TapiCore/Reader.cpp - Reader Tests -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "tapi/Core/Registry.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include "gtest/gtest.h"
#define DEBUG_TYPE "reader-test"

using namespace llvm;
using namespace tapi::internal;

TEST(Reader, diagnostic_reader) {
  Registry registry;
  registry.addDiagnosticReader();

  auto *input = "--- !tapi-tbd-v99\n...\n";
  auto inputMemBuf = MemoryBuffer::getMemBuffer(input);
  EXPECT_FALSE(!registry.canRead(*inputMemBuf));

  auto ret = registry.readFile(std::move(inputMemBuf));
  EXPECT_FALSE(ret);

  auto errorMsg = toString(ret.takeError());
  auto *expectedError = "unsupported tapi file type \'!tapi-tbd-v99\' in YAML";
  EXPECT_STREQ(errorMsg.c_str(), expectedError);
}
