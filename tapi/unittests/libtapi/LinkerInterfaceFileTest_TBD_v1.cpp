//===-- LinkerInterfaceFileTest_TBD_v1.cpp - Linker Interface File Test ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include "gtest/gtest.h"
#include <mach/machine.h>
#include <tapi/tapi.h>

using namespace tapi;

#define DEBUG_TYPE "libtapi-test"

static const char tbd_v1_file[] =
    "---\n"
    "archs: [ armv7, armv7s, armv7k, arm64 ]\n"
    "platform: ios\n"
    "install-name: Test.dylib\n"
    "current-version: 2.3.4\n"
    "compatibility-version: 1.0\n"
    "swift-version: 1.1\n"
    "exports:\n"
    "  - archs: [ armv7, armv7s, armv7k, arm64 ]\n"
    "    symbols: [ _sym1, _sym2, _sym3, _sym4, $ld$hide$os9.0$_sym1 ]\n"
    "    objc-classes: [ _class1, _class2 ]\n"
    "    objc-ivars: [ _class1._ivar1, _class1._ivar2 ]\n"
    "    weak-def-symbols: [ _weak1, _weak2 ]\n"
    "    thread-local-symbols: [ _tlv1, _tlv2 ]\n"
    "  - archs: [ armv7, armv7s, armv7k ]\n"
    "    symbols: [ _sym5 ]\n"
    "    objc-classes: [ _class3 ]\n"
    "    objc-ivars: [ _class1._ivar3 ]\n"
    "    weak-def-symbols: [ _weak3 ]\n"
    "    thread-local-symbols: [ _tlv3 ]\n"
    "...\n";

static const char tbd_v1_file3[] =
    "---\n"
    "archs: [ i386, x86_64 ]\n"
    "platform: macosx\n"
    "install-name: "
    "/System/Library/Frameworks/CoreImage.framework/Versions/A/CoreImage\n"
    "current-version: 5.0\n"
    "compatibility-version: 1.0.1\n"
    "exports:\n"
    "  - archs: [ i386, x86_64 ]\n"
    "    symbols: [ "
    "'$ld$install_name$os10.10$/System/Library/Frameworks/QuartzCore.framework/"
    "Versions/A/QuartzCore',\n"
    "               "
    "'$ld$install_name$os10.4$/System/Library/Frameworks/QuartzCore.framework/"
    "Versions/A/QuartzCore',\n"
    "               "
    "'$ld$install_name$os10.5$/System/Library/Frameworks/QuartzCore.framework/"
    "Versions/A/QuartzCore',\n"
    "               "
    "'$ld$install_name$os10.6$/System/Library/Frameworks/QuartzCore.framework/"
    "Versions/A/QuartzCore',\n"
    "               "
    "'$ld$install_name$os10.7$/System/Library/Frameworks/QuartzCore.framework/"
    "Versions/A/QuartzCore',\n"
    "               "
    "'$ld$install_name$os10.8$/System/Library/Frameworks/QuartzCore.framework/"
    "Versions/A/QuartzCore',\n"
    "               "
    "'$ld$install_name$os10.9$/System/Library/Frameworks/QuartzCore.framework/"
    "Versions/A/QuartzCore' ]\n"
    "...\n";

static const char tbd_v1_file_unknown_platform[] = "---\n"
                                                   "archs: [ i386 ]\n"
                                                   "platform: unknown\n"
                                                   "install-name: Test.dylib\n"
                                                   "...\n";

static const unsigned char unsupported_file[] = {0xcf, 0xfa, 0xed, 0xfe, 0x07,
                                                 0x00, 0x00, 0x00, 0x00, 0x00};

static const char malformed_file[] = "---\n"
                                     "archs: [ armv7, armv7s, armv7k, arm64 ]\n"
                                     "foobar: \"Unsupported key\"\n"
                                     "...\n";

static const char prefer_armv7[] = "---\n"
                                   "archs: [ armv7, armv7s, armv7k, arm64 ]\n"
                                   "platform: ios\n"
                                   "install-name: Test.dylib\n"
                                   "exports:\n"
                                   "  - archs: [ armv7 ]\n"
                                   "    symbols: [ _correct ]\n"
                                   "  - archs: [ armv7s, armv7k, arm64 ]\n"
                                   "    symbols: [ _incorrect ]\n"
                                   "...\n";

static const char prefer_armv7s[] = "---\n"
                                    "archs: [ armv7, armv7s, armv7k, arm64 ]\n"
                                    "platform: ios\n"
                                    "install-name: Test.dylib\n"
                                    "exports:\n"
                                    "  - archs: [ armv7s ]\n"
                                    "    symbols: [ _correct ]\n"
                                    "  - archs: [ armv7, armv7k, arm64 ]\n"
                                    "    symbols: [ _incorrect ]\n"
                                    "...\n";

static const char prefer_armv7k[] = "---\n"
                                    "archs: [ armv7, armv7s, armv7k, arm64 ]\n"
                                    "platform: ios\n"
                                    "install-name: Test.dylib\n"
                                    "exports:\n"
                                    "  - archs: [ armv7k ]\n"
                                    "    symbols: [ _correct ]\n"
                                    "  - archs: [ armv7, armv7s, arm64 ]\n"
                                    "    symbols: [ _incorrect ]\n"
                                    "...\n";

static const char prefer_arm64[] = "---\n"
                                   "archs: [ armv7, armv7s, armv7k, arm64 ]\n"
                                   "platform: ios\n"
                                   "install-name: Test.dylib\n"
                                   "exports:\n"
                                   "  - archs: [ arm64 ]\n"
                                   "    symbols: [ _correct ]\n"
                                   "  - archs: [ armv7, armv7s, armv7k ]\n"
                                   "    symbols: [ _incorrect ]\n"
                                   "...\n";

static const char prefer_i386[] = "---\n"
                                  "archs: [ i386, x86_64, x86_64h ]\n"
                                  "platform: macosx\n"
                                  "install-name: Test.dylib\n"
                                  "exports:\n"
                                  "  - archs: [ i386 ]\n"
                                  "    symbols: [ _correct ]\n"
                                  "  - archs: [ x86_64, x86_64h ]\n"
                                  "    symbols: [ _incorrect ]\n"
                                  "...\n";

static const char prefer_x86_64[] = "---\n"
                                    "archs: [ i386, x86_64, x86_64h ]\n"
                                    "platform: macosx\n"
                                    "install-name: Test.dylib\n"
                                    "exports:\n"
                                    "  - archs: [ x86_64 ]\n"
                                    "    symbols: [ _correct ]\n"
                                    "  - archs: [ i386, x86_64h ]\n"
                                    "    symbols: [ _incorrect ]\n"
                                    "...\n";

static const char prefer_x86_64h[] = "---\n"
                                     "archs: [ i386, x86_64, x86_64h ]\n"
                                     "platform: macosx\n"
                                     "install-name: Test.dylib\n"
                                     "exports:\n"
                                     "  - archs: [ x86_64h ]\n"
                                     "    symbols: [ _correct ]\n"
                                     "  - archs: [ i386, x86_64]\n"
                                     "    symbols: [ _incorrect ]\n"
                                     "...\n";

static const char fallback_armv7[] = "---\n"
                                     "archs: [ armv7 ]\n"
                                     "platform: ios\n"
                                     "install-name: Test.dylib\n"
                                     "exports:\n"
                                     "  - archs: [ armv7 ]\n"
                                     "    symbols: [ _correct ]\n"
                                     "...\n";

static const char fallback_armv7s[] = "---\n"
                                      "archs: [ armv7s ]\n"
                                      "platform: ios\n"
                                      "install-name: Test.dylib\n"
                                      "exports:\n"
                                      "  - archs: [ armv7s ]\n"
                                      "    symbols: [ _correct ]\n"
                                      "...\n";

static const char fallback_armv7k[] = "---\n"
                                      "archs: [ armv7k ]\n"
                                      "platform: ios\n"
                                      "install-name: Test.dylib\n"
                                      "exports:\n"
                                      "  - archs: [ armv7k ]\n"
                                      "    symbols: [ _correct ]\n"
                                      "...\n";

static const char fallback_arm64[] = "---\n"
                                     "archs: [ arm64 ]\n"
                                     "platform: ios\n"
                                     "install-name: Test.dylib\n"
                                     "exports:\n"
                                     "  - archs: [ arm64 ]\n"
                                     "    symbols: [ _correct ]\n"
                                     "...\n";

static const char fallback_i386[] = "---\n"
                                    "archs: [ i386 ]\n"
                                    "platform: macosx\n"
                                    "install-name: Test.dylib\n"
                                    "exports:\n"
                                    "  - archs: [ i386 ]\n"
                                    "    symbols: [ _correct ]\n"
                                    "...\n";

static const char fallback_x86_64[] = "---\n"
                                      "archs: [ x86_64 ]\n"
                                      "platform: macosx\n"
                                      "install-name: Test.dylib\n"
                                      "exports:\n"
                                      "  - archs: [ x86_64 ]\n"
                                      "    symbols: [ _correct ]\n"
                                      "...\n";

static const char fallback_x86_64h[] = "---\n"
                                       "archs: [ x86_64h ]\n"
                                       "platform: macosx\n"
                                       "install-name: Test.dylib\n"
                                       "exports:\n"
                                       "  - archs: [ x86_64h ]\n"
                                       "    symbols: [ _correct ]\n"
                                       "...\n";

using ExportedSymbol = std::tuple<std::string, bool, bool>;
using ExportedSymbolSeq = std::vector<ExportedSymbol>;

inline bool operator<(const ExportedSymbol &lhs, const ExportedSymbol &rhs) {
  return std::get<0>(lhs) < std::get<0>(rhs);
}

static ExportedSymbolSeq tbd_v1_arm_exports = {
    {"_OBJC_CLASS_$_class1", false, false},
    {"_OBJC_CLASS_$_class2", false, false},
    {"_OBJC_CLASS_$_class3", false, false},
    {"_OBJC_IVAR_$_class1._ivar1", false, false},
    {"_OBJC_IVAR_$_class1._ivar2", false, false},
    {"_OBJC_IVAR_$_class1._ivar3", false, false},
    {"_OBJC_METACLASS_$_class1", false, false},
    {"_OBJC_METACLASS_$_class2", false, false},
    {"_OBJC_METACLASS_$_class3", false, false},
    {"_sym2", false, false},
    {"_sym3", false, false},
    {"_sym4", false, false},
    {"_sym5", false, false},
    {"_tlv1", false, true},
    {"_tlv2", false, true},
    {"_tlv3", false, true},
    {"_weak1", true, false},
    {"_weak2", true, false},
    {"_weak3", true, false},
};

static ExportedSymbolSeq tbd_v1_arm64_exports = {
    {"_OBJC_CLASS_$_class1", false, false},
    {"_OBJC_CLASS_$_class2", false, false},
    {"_OBJC_IVAR_$_class1._ivar1", false, false},
    {"_OBJC_IVAR_$_class1._ivar2", false, false},
    {"_OBJC_METACLASS_$_class1", false, false},
    {"_OBJC_METACLASS_$_class2", false, false},
    {"_sym2", false, false},
    {"_sym3", false, false},
    {"_sym4", false, false},
    {"_tlv1", false, true},
    {"_tlv2", false, true},
    {"_weak1", true, false},
    {"_weak2", true, false},
};

namespace TBDv1 {

TEST(libtapiTBDv1, LIF_isSupported) {
  llvm::StringRef buffer(tbd_v1_file);
  bool isSupported1 = LinkerInterfaceFile::isSupported(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size());
  bool isSupported2 = LinkerInterfaceFile::isSupported(
      "Test.tbd", reinterpret_cast<const uint8_t *>(unsupported_file),
      sizeof(unsupported_file));
  ASSERT_TRUE(isSupported1);
  ASSERT_FALSE(isSupported2);
}

// Test parsing a .tbd file from a memory buffer/nmapped file
TEST(libtapiTBDv1, LIF_Load_ARM) {
  llvm::StringRef buffer(tbd_v1_file);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ(FileType::TBD_V1, file->getFileType());
  EXPECT_EQ(Platform::iOS, file->getPlatform());
  EXPECT_EQ(std::string("Test.dylib"), file->getInstallName());
  EXPECT_EQ(0x20304U, file->getCurrentVersion());
  EXPECT_EQ(0x10000U, file->getCompatibilityVersion());
  EXPECT_EQ(2U, file->getSwiftVersion());

  ExportedSymbolSeq exports;
  for (const auto &sym : file->exports())
    exports.emplace_back(sym.getName(), sym.isWeakDefined(),
                         sym.isThreadLocalValue());
  std::sort(exports.begin(), exports.end());

  ASSERT_EQ(tbd_v1_arm_exports.size(), exports.size());
  EXPECT_TRUE(
      std::equal(exports.begin(), exports.end(), tbd_v1_arm_exports.begin()));
}

TEST(libtapiTBDv1, LIF_Load_ARM64) {
  llvm::StringRef buffer(tbd_v1_file);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ(FileType::TBD_V1, file->getFileType());
  EXPECT_EQ(Platform::iOS, file->getPlatform());
  EXPECT_EQ(std::string("Test.dylib"), file->getInstallName());
  EXPECT_EQ(0x20304U, file->getCurrentVersion());
  EXPECT_EQ(0x10000U, file->getCompatibilityVersion());
  EXPECT_EQ(2U, file->getSwiftVersion());

  ExportedSymbolSeq exports;
  for (const auto &sym : file->exports())
    exports.emplace_back(sym.getName(), sym.isWeakDefined(),
                         sym.isThreadLocalValue());
  std::sort(exports.begin(), exports.end());

  ASSERT_EQ(tbd_v1_arm64_exports.size(), exports.size());
  EXPECT_TRUE(
      std::equal(exports.begin(), exports.end(), tbd_v1_arm64_exports.begin()));
}

TEST(libtapiTBDv1, LIF_Load_Install_Name) {
  llvm::StringRef buffer(tbd_v1_file3);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "/System/Library/Frameworks/CoreImage.framework/Versions/A/CoreImage.tbd",
      reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
      CPU_TYPE_X86, CPU_SUBTYPE_X86_ALL, CpuSubTypeMatching::ABI_Compatible,
      PackedVersion32(10, 10, 0), errorMessage));
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_NE(nullptr, file);
  ASSERT_EQ(FileType::TBD_V1, file->getFileType());
  EXPECT_EQ(Platform::OSX, file->getPlatform());
  EXPECT_EQ(std::string("/System/Library/Frameworks/QuartzCore.framework/"
                        "Versions/A/QuartzCore"),
            file->getInstallName());
  EXPECT_EQ(0x50000U, file->getCurrentVersion());
  EXPECT_EQ(0x10001U, file->getCompatibilityVersion());
  EXPECT_TRUE(file->isApplicationExtensionSafe());
  EXPECT_TRUE(file->hasTwoLevelNamespace());
  EXPECT_TRUE(file->isInstallNameVersionSpecific());
}

TEST(libtapiTBDv1, LIF_Load_Unknown_Platform) {
  llvm::StringRef buffer(tbd_v1_file_unknown_platform);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_I386, CPU_SUBTYPE_I386_ALL,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ(FileType::TBD_V1, file->getFileType());
  EXPECT_EQ(Platform::Unknown, file->getPlatform());
  EXPECT_EQ(std::string("Test.dylib"), file->getInstallName());
}

// Test for invalid files.
TEST(libtapiTBDv1, LIF_UnsupportedFileType) {
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(unsupported_file),
      sizeof(unsupported_file), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("unsupported file type", errorMessage);
}

TEST(libtapiTBDv1, LIF_MalformedFile) {
  llvm::StringRef buffer(malformed_file);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("malformed file\nTest.tbd:2:1: error: missing required key "
            "'platform'\narchs: [ armv7, armv7s, armv7k, arm64 ]\n^\n",
            errorMessage);
}

TEST(libtapiTBDv1, LIF_ArchitectureNotFound) {
  llvm::StringRef buffer(tbd_v1_file);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_X86_64, CPU_SUBTYPE_X86_ALL,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("missing required architecture x86_64 in file Test.tbd (4 slices)",
            errorMessage);
}

TEST(libtapiTBDv1, LIF_SelectPreferedSlice_armv7) {
  llvm::StringRef buffer(prefer_armv7);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
}

TEST(libtapiTBDv1, LIF_SelectPreferedSlice_armv7s) {
  llvm::StringRef buffer(prefer_armv7s);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7S,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
}

TEST(libtapiTBDv1, LIF_SelectPreferedSlice_armv7k) {
  llvm::StringRef buffer(prefer_armv7k);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7K,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
}

TEST(libtapiTBDv1, LIF_SelectPreferedSlice_arm64) {
  llvm::StringRef buffer(prefer_arm64);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
}

TEST(libtapiTBDv1, LIF_SelectPreferedSlice_i386) {
  llvm::StringRef buffer(prefer_i386);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_I386, CPU_SUBTYPE_386,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
}

TEST(libtapiTBDv1, LIF_SelectPreferedSlice_x86_64) {
  llvm::StringRef buffer(prefer_x86_64);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_ALL,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
}

TEST(libtapiTBDv1, LIF_SelectPreferedSlice_x86_64h) {
  llvm::StringRef buffer(prefer_x86_64h);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_H,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
}

TEST(libtapiTBDv1, LIF_FallBack_armv7) {
  llvm::StringRef buffer(fallback_armv7);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());

  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7S,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());

  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7K,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  // BEGIN WORKAROUND FOR rdar://problem/25535679
  // ASSERT_EQ(nullptr, file);
  // ASSERT_EQ("missing required architecture armv7k in file Test.tbd",
  //           errorMessage);
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
  // END WORKAROUND

  errorMessage.clear();
  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("missing required architecture arm64 in file Test.tbd",
            errorMessage);
}

TEST(libtapiTBDv1, LIF_FallBack_armv7s) {
  llvm::StringRef buffer(fallback_armv7s);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());

  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7S,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());

  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7K,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  // BEGIN WORKAROUND FOR rdar://problem/25535679
  // ASSERT_EQ(nullptr, file);
  // ASSERT_EQ("missing required architecture armv7k in file Test.tbd",
  //           errorMessage);
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
  // END WORKAROUND

  errorMessage.clear();
  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("missing required architecture arm64 in file Test.tbd",
            errorMessage);
}

TEST(libtapiTBDv1, LIF_FallBack_armv7k) {
  llvm::StringRef buffer(fallback_armv7k);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  // BEGIN WORKAROUND FOR rdar://problem/25535679
  // ASSERT_EQ(nullptr, file);
  // ASSERT_EQ("missing required architecture armv7 in file Test.tbd",
  //           errorMessage);
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
  // END WORKAROUND

  errorMessage.clear();
  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7S,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  // BEGIN WORKAROUND FOR rdar://problem/25535679
  // ASSERT_EQ(nullptr, file);
  // ASSERT_EQ("missing required architecture armv7s in file Test.tbd",
  //           errorMessage);
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
  // END WORKAROUND

  errorMessage.clear();
  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7K,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());

  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("missing required architecture arm64 in file Test.tbd",
            errorMessage);
}

TEST(libtapiTBDv1, LIF_FallBack_arm64) {
  llvm::StringRef buffer(fallback_arm64);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("missing required architecture armv7 in file Test.tbd",
            errorMessage);

  errorMessage.clear();
  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7S,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("missing required architecture armv7s in file Test.tbd",
            errorMessage);

  errorMessage.clear();
  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM, CPU_SUBTYPE_ARM_V7K,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("missing required architecture armv7k in file Test.tbd",
            errorMessage);

  errorMessage.clear();
  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(9, 0, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
}

TEST(libtapiTBDv1, LIF_FallBack_i386) {
  llvm::StringRef buffer(fallback_i386);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_I386, CPU_SUBTYPE_386,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());

  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_ALL,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("missing required architecture x86_64 in file Test.tbd",
            errorMessage);

  errorMessage.clear();
  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_H,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("missing required architecture x86_64h in file Test.tbd",
            errorMessage);
}

TEST(libtapiTBDv1, LIF_FallBack_x86_64) {
  llvm::StringRef buffer(fallback_x86_64);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_I386, CPU_SUBTYPE_386,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("missing required architecture i386 in file Test.tbd",
            errorMessage);

  errorMessage.clear();
  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_ALL,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());

  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_H,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
}

TEST(libtapiTBDv1, LIF_FallBack_x86_64h) {
  llvm::StringRef buffer(fallback_x86_64h);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_I386, CPU_SUBTYPE_386,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("missing required architecture i386 in file Test.tbd",
            errorMessage);

  errorMessage.clear();
  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_ALL,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());

  file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_H,
      CpuSubTypeMatching::ABI_Compatible, PackedVersion32(10, 11, 0),
      errorMessage));
  ASSERT_NE(nullptr, file);
  ASSERT_TRUE(errorMessage.empty());
  ASSERT_EQ("_correct", file->exports().front().getName());
}

TEST(libtapiTBDv1, LIF_No_FallBack_x86_64h) {
  llvm::StringRef buffer(fallback_x86_64h);
  std::string errorMessage;
  auto file = std::unique_ptr<LinkerInterfaceFile>(LinkerInterfaceFile::create(
      "Test.tbd", reinterpret_cast<const uint8_t *>(buffer.data()),
      buffer.size(), CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_ALL,
      CpuSubTypeMatching::Exact, PackedVersion32(10, 11, 0), errorMessage));
  ASSERT_EQ(nullptr, file);
  ASSERT_EQ("missing required architecture x86_64 in file Test.tbd",
            errorMessage);
}

} // end namespace TBDv1

#pragma clang diagnostic pop