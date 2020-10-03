//===-- LinkerInterfaceFileTest_TBD_v4.cpp - Linker Interface File Test ---===//
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
#include <mach/machine.h>
#include <tapi/tapi.h>

using namespace tapi;

#define DEBUG_TYPE "libtapi-test"

static const char tbd_v4_file[] =
    "--- !tapi-tbd\n"
    "tbd-version: 4\n"
    "targets:  [ i386-macos, x86_64-macos, x86_64-ios-maccatalyst ]\n"
    "uuids:\n"
    "  - target: i386-macos\n"
    "    value:  00000000-0000-0000-0000-000000000000\n"
    "  - target: x86_64-macos\n"
    "    value:  11111111-1111-1111-1111-111111111111\n"
    "  - target: x86_64-<6>\n"
    "    value: 11111111-1111-1111-1111-111111111111\n"
    "flags: [ flat_namespace, installapi ]\n"
    "install-name: /System/Library/Frameworks/Umbrella.framework/Umbrella\n"
    "current-version: 1.2.3\n"
    "compatibility-version: 1.2\n"
    "swift-abi-version: 5\n"
    "parent-umbrella:\n"
    "  - targets: [ i386-macos, x86_64-macos, x86_64-ios-maccatalyst ]\n"
    "    umbrella: System\n"
    "allowable-clients:\n"
    "  - targets: [ i386-macos, x86_64-macos, x86_64-ios-maccatalyst ]\n"
    "    clients: [ ClientA, ClientB ]\n"
    "reexported-libraries:\n"
    "  - targets: [ i386-macos ]\n"
    "    libraries: [ /System/Library/Frameworks/A.framework/A ]\n"
    "  - targets: [ x86_64-macos, x86_64-ios-maccatalyst ]\n"
    "    libraries: [ /System/Library/Frameworks/B.framework/B,\n"
    "                 /System/Library/Frameworks/C.framework/C ]\n"
    "exports:\n"
    "  - targets: [ i386-macos ]\n"
    "    symbols: [ _symA ]\n"
    "    objc-classes: []\n"
    "    objc-eh-types: []\n"
    "    objc-ivars: []\n"
    "    weak-symbols: []\n"
    "    thread-local-symbols: []\n"
    "  - targets: [ x86_64-ios-maccatalyst]\n"
    "    symbols: [_symB]\n"
    "  - targets: [ x86_64-macos, x86_64-ios-maccatalyst ]\n"
    "    symbols: [_symAB]\n"
    "reexports:\n"
    "  - targets: [i386 - macos]\n"
    "    symbols: [_symC]\n"
    "    objc-classes: []\n"
    "    objc-eh-types: []\n"
    "    objc-ivars: []\n"
    "    weak-symbols: []\n"
    "    thread-local-symbols: []\n"
    "undefineds:\n"
    "  - targets: [ i386-macos ]\n"
    "    symbols: [ _symD ]\n"
    "    objc-classes: []\n"
    "    objc-eh-types: []\n"
    "    objc-ivars: []\n"
    "    weak-symbols: []\n"
    "    thread-local-symbols: []\n"
    "...\n";

using ExportedSymbol = std::tuple<std::string, bool, bool>;
using ExportedSymbolSeq = std::vector<ExportedSymbol>;

inline bool operator<(const ExportedSymbol &lhs, const ExportedSymbol &rhs) {
  return std::get<0>(lhs) < std::get<0>(rhs);
}

namespace TBDv4 {

TEST(libtapiTBDv4, LIF_isSupported) {
  llvm::StringRef buffer(tbd_v4_file);
  bool isSupported = LinkerInterfaceFile::isSupported(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size());
  EXPECT_TRUE(isSupported);
}

// Test parsing a .tbd file from a memory buffer/nmapped file
TEST(libtapiTBDv4, LIF_Load) {
  llvm::StringRef buffer(tbd_v4_file);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "/System/Library/Frameworks/Umbrella.framework/Umbrella.tbd",
      reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
      CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_ALL, ParsingFlags::None,
      PackedVersion32(10, 15, 0), errorMessage));
  llvm::outs() << errorMessage;
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(Platform::zippered, file->getPlatform());
  EXPECT_EQ(
      std::string("/System/Library/Frameworks/Umbrella.framework/Umbrella"),
      file->getInstallName());
  EXPECT_TRUE(file->isApplicationExtensionSafe());
  EXPECT_FALSE(file->hasTwoLevelNamespace());
  EXPECT_TRUE(file->hasReexportedLibraries());
}

TEST(libtapiTBDv4, LIF_Platform_macOS) {
  static const char tbd_v4_macos[] =
      "--- !tapi-tbd\n"
      "tbd-version: 4\n"
      "targets: [ x86_64-macos ]\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v4_macos);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_X86_64, CPU_SUBTYPE_X86_ALL,
      CpuSubTypeMatching::Exact, PackedVersion32(10, 12, 0), errorMessage));
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(Platform::OSX, file->getPlatform());
}

TEST(libtapiTBDv4, LIF_Platform_iOS) {
  static const char tbd_v4_ios[] =
      "--- !tapi-tbd\n"
      "tbd-version: 4\n"
      "targets: [ arm64-ios ]\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v4_ios);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
      CpuSubTypeMatching::Exact, PackedVersion32(10, 0, 0), errorMessage));
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(Platform::iOS, file->getPlatform());
}

TEST(libtapiTBDv4, LIF_Platform_watchOS) {
  static const char tbd_v4_watchos[] =
      "--- !tapi-tbd\n"
      "tbd-version: 4\n"
      "targets: [ armv7k-watchos ]\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v4_watchos);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7K,
      CpuSubTypeMatching::Exact, PackedVersion32(3, 0, 0), errorMessage));
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(Platform::watchOS, file->getPlatform());
}

TEST(libtapiTBDv4, LIF_Platform_tvOS) {
  static const char tbd_v4_tvos[] =
      "--- !tapi-tbd\n"
      "tbd-version: 4\n"
      "targets: [ arm64-tvos ]\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v4_tvos);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
      CpuSubTypeMatching::Exact, PackedVersion32(10, 0, 0), errorMessage));
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(Platform::tvOS, file->getPlatform());
}

TEST(libtapiTBDv4, LIF_Platform_bridgeOS) {
  static const char tbd_v4_bridgeos[] =
      "--- !tapi-tbd\n"
      "tbd-version: 4\n"
      "targets: [ arm64-bridgeos ]\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v4_bridgeos);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
      CpuSubTypeMatching::Exact, PackedVersion32(2, 0, 0), errorMessage));
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(Platform::bridgeOS, file->getPlatform());
}

TEST(libtapiTBDv4, LIF_Load_iosmac1) {
  static const char tbd_v4_iosmac[] =
      "--- !tapi-tbd\n"
      "tbd-version: 4\n"
      "targets: [ x86_64-ios-macabi ]\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v4_iosmac);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "/System/Library/Frameworks/Foo.framework/Foo.tbd",
      reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
      CPU_TYPE_X86_64, CPU_SUBTYPE_X86_ALL, ParsingFlags::None,
      PackedVersion32(10, 14, 0), errorMessage));
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(Platform::iOSMac, file->getPlatform());
  EXPECT_TRUE(file->exports().empty());
}

TEST(libtapiTBDv4, LIF_Load_iosmac2) {
  static const char tbd_v4_iosmac[] =
      "--- !tapi-tbd\n"
      "tbd-version: 4\n"
      "targets: [ x86_64-<6> ]\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v4_iosmac);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "/System/Library/Frameworks/Foo.framework/Foo.tbd",
      reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
      CPU_TYPE_X86_64, CPU_SUBTYPE_X86_ALL, ParsingFlags::None,
      PackedVersion32(10, 14, 0), errorMessage));
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(Platform::iOSMac, file->getPlatform());
  EXPECT_TRUE(file->exports().empty());
}

TEST(libtapiTBDv4, LIF_Load_iosmac3) {
  static const char tbd_v4_iosmac[] =
      "--- !tapi-tbd\n"
      "tbd-version: 4\n"
      "targets: [ x86_64-ios-maccatalyst ]\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "...\n";
  llvm::StringRef buffer(tbd_v4_iosmac);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "/System/Library/Frameworks/Foo.framework/Foo.tbd",
      reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
      CPU_TYPE_X86_64, CPU_SUBTYPE_X86_ALL, ParsingFlags::None,
      PackedVersion32(10, 14, 0), errorMessage));
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(Platform::iOSMac, file->getPlatform());
  EXPECT_TRUE(file->exports().empty());
}

TEST(libtapiTBDv4, LIF_Load_zippered) {
  static const char tbd_v4_zippered[] =
      "--- !tapi-tbd\n"
      "tbd-version: 4\n"
      "targets: [ i386-macos, x86_64-macos, x86_64-ios-maccatalyst ]\n"
      "install-name: /System/Library/Frameworks/Foo.framework/Foo\n"
      "exports:\n"
      "  - targets: [ i386-macos, x86_64-macos, x86_64-uikitformac ]\n"
      "    objc-classes: [ Foo, Bar ]\n"
      "...\n";

  static std::vector<std::string> tbd_v4_i386_symbols = {
      ".objc_class_name_Bar",
      ".objc_class_name_Foo",
  };

  static std::vector<std::string> tbd_v4_x86_64_symbols = {
      "_OBJC_CLASS_$_Bar",
      "_OBJC_CLASS_$_Foo",
      "_OBJC_METACLASS_$_Bar",
      "_OBJC_METACLASS_$_Foo",
  };

  std::vector<std::string> exports;
  llvm::StringRef buffer(tbd_v4_zippered);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "/System/Library/Frameworks/Foo.framework/Foo.tbd",
      reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
      CPU_TYPE_I386, CPU_SUBTYPE_I386_ALL, ParsingFlags::None,
      PackedVersion32(10, 14, 0), errorMessage));
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(Platform::zippered, file->getPlatform());

  exports.clear();
  for (const auto &sym : file->exports())
    exports.emplace_back(sym.getName());
  std::sort(exports.begin(), exports.end());

  ASSERT_EQ(tbd_v4_i386_symbols.size(), exports.size());
  EXPECT_TRUE(
      std::equal(exports.begin(), exports.end(), tbd_v4_i386_symbols.begin()));

  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "/System/Library/Frameworks/Foo.framework/Foo.tbd",
      reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
      CPU_TYPE_X86_64, CPU_SUBTYPE_X86_ALL, ParsingFlags::None,
      PackedVersion32(10, 14, 0), errorMessage));
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_NE(nullptr, file);
  EXPECT_EQ(Platform::zippered, file->getPlatform());

  exports.clear();
  for (const auto &sym : file->exports())
    exports.emplace_back(sym.getName());
  std::sort(exports.begin(), exports.end());

  ASSERT_EQ(tbd_v4_x86_64_symbols.size(), exports.size());
  EXPECT_TRUE(std::equal(exports.begin(), exports.end(),
                         tbd_v4_x86_64_symbols.begin()));
}

} // end namespace TBDv4

#pragma clang diagnostic pop