//===-- LinkerInterfaceFileTest_TBD_v3.cpp - Linker Interface File Test ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include "tapi/Core/ArchitectureConfig.h"
#include "gtest/gtest.h"
#include <mach-o/loader.h>
#include <mach/machine.h>
#include <tapi/tapi.h>

using namespace tapi;

#define DEBUG_TYPE "libtapi-test"

#ifndef PLATFORM_MACCATALYST
#define PLATFORM_MACCATALYST 6
#endif

static const char tbd_v3_file[] =
    "--- !tapi-tbd-v3\n"
    "archs: [ arm64 ]\n"
    "platform: ios\n"
    "install-name: /System/Library/Frameworks/Umbrella.framework/Umbrella\n"
    "exports:\n"
    "  - archs: [ arm64 ]\n"
    "    re-exports: [ /System/Library/PrivateFrameworks/Sub1.framework/Sub1, "
    "\n"
    "                  /System/Library/PrivateFrameworks/Sub2.framework/Sub2 "
    "]\n"
    "--- !tapi-tbd-v3\n"
    "archs: [ arm64 ]\n"
    "platform: ios\n"
    "install-name: /System/Library/PrivateFrameworks/Sub1.framework/Sub1\n"
    "exports:\n"
    "  - archs: [ arm64 ]\n"
    "    symbols: [ _sym1 ]\n"
    "--- !tapi-tbd-v3\n"
    "archs: [ arm64 ]\n"
    "platform: ios\n"
    "install-name: /System/Library/PrivateFrameworks/Sub2.framework/Sub2\n"
    "exports:\n"
    "  - archs: [ arm64 ]\n"
    "    symbols: [ _sym2 ]\n"
    "...\n";

using ExportedSymbol = std::tuple<std::string, bool, bool>;
using ExportedSymbolSeq = std::vector<ExportedSymbol>;

inline bool operator<(const ExportedSymbol &lhs, const ExportedSymbol &rhs) {
  return std::get<0>(lhs) < std::get<0>(rhs);
}

namespace TBDv3 {

TEST(libtapiTBDv3, LIF_isSupported) {
  llvm::StringRef buffer(tbd_v3_file);
  bool isSupported = LinkerInterfaceFile::isSupported(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size());
  EXPECT_TRUE(isSupported);
}

// Test parsing a .tbd file from a memory buffer/nmapped file
TEST(libtapiTBDv3, LIF_Load_ARM64) {
  static const std::vector<std::string> expectedInlinedFrameworkNames = {
      "/System/Library/PrivateFrameworks/Sub1.framework/Sub1",
      "/System/Library/PrivateFrameworks/Sub2.framework/Sub2"};

  llvm::StringRef buffer(tbd_v3_file);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "/System/Library/Frameworks/Umbrella.framework/Umbrella.tbd",
      reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
      CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL, ParsingFlags::None,
      PackedVersion32(9, 0, 0), errorMessage));
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(FileType::TBD_V3, file->getFileType());
  EXPECT_EQ(Platform::iOS, file->getPlatform());
  EXPECT_EQ(
      std::string("/System/Library/Frameworks/Umbrella.framework/Umbrella"),
      file->getInstallName());
  EXPECT_TRUE(file->isApplicationExtensionSafe());
  EXPECT_TRUE(file->hasTwoLevelNamespace());
  EXPECT_TRUE(file->hasReexportedLibraries());
  EXPECT_TRUE(file->exports().empty());
  EXPECT_EQ(2U, file->inlinedFrameworkNames().size());
  std::vector<std::string> inlinedFrameworkNames;
  for (auto &name : file->inlinedFrameworkNames())
    inlinedFrameworkNames.emplace_back(name);
  std::sort(inlinedFrameworkNames.begin(), inlinedFrameworkNames.end());
  ASSERT_EQ(expectedInlinedFrameworkNames.size(), inlinedFrameworkNames.size());
  EXPECT_TRUE(std::equal(inlinedFrameworkNames.begin(),
                         inlinedFrameworkNames.end(),
                         expectedInlinedFrameworkNames.begin()));

  for (auto &name : file->inlinedFrameworkNames()) {
    auto framework =
        std::unique_ptr<LinkerInterfaceFile>(file->getInlinedFramework(
            name, CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL, ParsingFlags::None,
            PackedVersion32(9, 0, 0), errorMessage));
    ASSERT_TRUE(errorMessage.empty());
    ASSERT_NE(nullptr, framework);
    EXPECT_EQ(FileType::TBD_V3, framework->getFileType());
    EXPECT_EQ(Platform::iOS, framework->getPlatform());
    EXPECT_TRUE(framework->isApplicationExtensionSafe());
    EXPECT_TRUE(framework->hasTwoLevelNamespace());
    EXPECT_FALSE(framework->hasReexportedLibraries());
    EXPECT_EQ(1U, framework->exports().size());
    EXPECT_TRUE(framework->inlinedFrameworkNames().empty());
  }
}

TEST(libtapiTBDv3, LIF_Platform_macOS) {
  static const char tbd_v3_macos[] =
      "--- !tapi-tbd-v3\n"
      "archs: [ x86_64 ]\n"
      "platform: macosx\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v3_macos);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_X86_64, CPU_SUBTYPE_X86_ALL,
      CpuSubTypeMatching::Exact, PackedVersion32(10, 12, 0), errorMessage));
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(FileType::TBD_V3, file->getFileType());
  EXPECT_EQ(Platform::OSX, file->getPlatform());
  EXPECT_EQ(std::vector<uint32_t>{PLATFORM_MACOS}, file->getPlatformSet());
}

TEST(libtapiTBDv3, LIF_Platform_iOS) {
  static const char tbd_v3_ios[] =
      "--- !tapi-tbd-v3\n"
      "archs: [ arm64 ]\n"
      "platform: ios\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v3_ios);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
      CpuSubTypeMatching::Exact, PackedVersion32(10, 0, 0), errorMessage));
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(FileType::TBD_V3, file->getFileType());
  EXPECT_EQ(Platform::iOS, file->getPlatform());
  EXPECT_EQ(std::vector<uint32_t>{PLATFORM_IOS}, file->getPlatformSet());
}

TEST(libtapiTBDv3, LIF_Platform_watchOS) {
  static const char tbd_v3_watchos[] =
      "--- !tapi-tbd-v3\n"
      "archs: [ armv7k ]\n"
      "platform: watchos\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v3_watchos);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7K,
      CpuSubTypeMatching::Exact, PackedVersion32(3, 0, 0), errorMessage));
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(FileType::TBD_V3, file->getFileType());
  EXPECT_EQ(Platform::watchOS, file->getPlatform());
  EXPECT_EQ(std::vector<uint32_t>{PLATFORM_WATCHOS}, file->getPlatformSet());
}

TEST(libtapiTBDv3, LIF_Platform_tvOS) {
  static const char tbd_v3_tvos[] =
      "--- !tapi-tbd-v3\n"
      "archs: [ arm64 ]\n"
      "platform: tvos\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v3_tvos);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
      CpuSubTypeMatching::Exact, PackedVersion32(10, 0, 0), errorMessage));
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(FileType::TBD_V3, file->getFileType());
  EXPECT_EQ(Platform::tvOS, file->getPlatform());
  EXPECT_EQ(std::vector<uint32_t>{PLATFORM_TVOS}, file->getPlatformSet());
}

TEST(libtapiTBDv3, LIF_Platform_bridgeOS) {
  static const char tbd_v3_bridgeos[] =
      "--- !tapi-tbd-v3\n"
      "archs: [ arm64 ]\n"
      "platform: bridgeos\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v3_bridgeos);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
      CpuSubTypeMatching::Exact, PackedVersion32(2, 0, 0), errorMessage));
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(FileType::TBD_V3, file->getFileType());
  EXPECT_EQ(Platform::bridgeOS, file->getPlatform());
  EXPECT_EQ(std::vector<uint32_t>{PLATFORM_BRIDGEOS}, file->getPlatformSet());
}

TEST(libtapiTBDv3, LIF_Load_iosmac) {
  static const char tbd_v3_iosmac[] =
      "--- !tapi-tbd-v3\n"
      "archs: [ x86_64 ]\n"
      "platform: iosmac\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v3_iosmac);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "/System/Library/Frameworks/Foo.framework/Foo.tbd",
      reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
      CPU_TYPE_X86_64, CPU_SUBTYPE_X86_ALL, ParsingFlags::None,
      PackedVersion32(10, 14, 0), errorMessage));
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(FileType::TBD_V3, file->getFileType());
  EXPECT_EQ(Platform::iOSMac, file->getPlatform());
  EXPECT_EQ(std::vector<uint32_t>{PLATFORM_MACCATALYST},
            file->getPlatformSet());
  EXPECT_TRUE(file->exports().empty());
}

TEST(libtapiTBDv3, LIF_Load_zippered) {
  static const char tbd_v3_zippered[] =
      "--- !tapi-tbd-v3\n"
      "archs: [ i386, x86_64 ]\n"
      "platform: zippered\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "exports:\n"
      "  - archs: [ i386, x86_64 ]\n"
      "    objc-classes: [ Foo, Bar ]\n"
      "...\n";

  static std::vector<std::string> tbd_v3_i386_symbols = {
      ".objc_class_name_Bar",
      ".objc_class_name_Foo",
  };

  static std::vector<std::string> tbd_v3_x86_64_symbols = {
      "_OBJC_CLASS_$_Bar",
      "_OBJC_CLASS_$_Foo",
      "_OBJC_METACLASS_$_Bar",
      "_OBJC_METACLASS_$_Foo",
  };

  std::vector<std::string> exports;
  llvm::StringRef buffer(tbd_v3_zippered);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "/System/Library/Frameworks/Foo.framework/Foo.tbd",
      reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
      CPU_TYPE_I386, CPU_SUBTYPE_I386_ALL, ParsingFlags::None,
      PackedVersion32(10, 14, 0), errorMessage));
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(FileType::TBD_V3, file->getFileType());
  EXPECT_EQ(Platform::zippered, file->getPlatform());
  EXPECT_EQ((std::vector<uint32_t>{PLATFORM_MACOS, PLATFORM_MACCATALYST}),
            file->getPlatformSet());

  exports.clear();
  for (const auto &sym : file->exports())
    exports.emplace_back(sym.getName());
  std::sort(exports.begin(), exports.end());

  ASSERT_EQ(tbd_v3_i386_symbols.size(), exports.size());
  EXPECT_TRUE(
      std::equal(exports.begin(), exports.end(), tbd_v3_i386_symbols.begin()));

  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "/System/Library/Frameworks/Foo.framework/Foo.tbd",
      reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
      CPU_TYPE_X86_64, CPU_SUBTYPE_X86_ALL, ParsingFlags::None,
      PackedVersion32(10, 14, 0), errorMessage));
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(FileType::TBD_V3, file->getFileType());
  EXPECT_EQ(Platform::zippered, file->getPlatform());
  EXPECT_EQ((std::vector<uint32_t>{PLATFORM_MACOS, PLATFORM_MACCATALYST}),
            file->getPlatformSet());

  exports.clear();
  for (const auto &sym : file->exports())
    exports.emplace_back(sym.getName());
  std::sort(exports.begin(), exports.end());

  ASSERT_EQ(tbd_v3_x86_64_symbols.size(), exports.size());
  EXPECT_TRUE(std::equal(exports.begin(), exports.end(),
                         tbd_v3_x86_64_symbols.begin()));
}


TEST(libtapiTBDv3, LIF_Load_maccatalyst) {
  static const char tbd_v3_maccatalyst[] =
      "--- !tapi-tbd-v3\n"
      "archs: [ x86_64 ]\n"
      "platform: maccatalyst\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v3_maccatalyst);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "/System/Library/Frameworks/Foo.framework/Foo.tbd",
      reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
      CPU_TYPE_X86_64, CPU_SUBTYPE_X86_ALL, ParsingFlags::None,
      PackedVersion32(10, 14, 0), errorMessage));
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(FileType::TBD_V3, file->getFileType());
  EXPECT_EQ(Platform::iOSMac, file->getPlatform());
  EXPECT_EQ(std::vector<uint32_t>{PLATFORM_MACCATALYST},
            file->getPlatformSet());
  EXPECT_TRUE(file->exports().empty());
}

} // end namespace TBDv3

#pragma clang diagnostic pop
