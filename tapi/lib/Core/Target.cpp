//===- tapi/Core/Target.cpp - Target ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "tapi/Core/Target.h"
#include "tapi/Core/LLVM.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

Expected<Target> Target::create(StringRef target) {
  auto result = target.split('-');
  auto architectureStr = result.first;
  auto architecture = getArchType(architectureStr);
  auto platformStr = result.second;
  Platform platform;
  platform = StringSwitch<Platform>(platformStr)
                 .Case("macos", Platform::macOS)
                 .Case("ios", Platform::iOS)
                 .Case("tvos", Platform::tvOS)
                 .Case("watchos", Platform::watchOS)
                 .Case("ios-macabi", Platform::macCatalyst)
                 .Case("ios-maccatalyst", Platform::macCatalyst)
                 .Case("ios-simulator", Platform::iOSSimulator)
                 .Case("tvos-simulator", Platform::tvOSSimulator)
                 .Case("watchos-simulator", Platform::watchOSSimulator)
                 .Default(Platform::unknown);

  if (platform == Platform::unknown) {
    if (platformStr.startswith("<") && platformStr.endswith(">")) {
      platformStr = platformStr.drop_front().drop_back();
      unsigned long long rawValue;
      if (platformStr.getAsInteger(10, rawValue))
        return make_error<StringError>("invalid platform number",
                                       inconvertibleErrorCode());

      platform = (Platform)rawValue;
    }
  }

  return Target{architecture, platform};
}

Target::operator std::string() const {
  return (getArchName(architecture) + " (" + getPlatformName(platform) + ")")
      .str();
}

PlatformSet mapToPlatformSet(ArrayRef<Target> targets) {
  PlatformSet result;
  for (const auto &target : targets)
    result.emplace(target.platform);
  return result;
}

ArchitectureSet mapToArchitectureSet(ArrayRef<Target> targets) {
  ArchitectureSet result;
  for (const auto &target : targets)
    result.set(target.architecture);
  return result;
}

raw_ostream &operator<<(raw_ostream &os, const Target &target) {
  os << std::string(target);
  return os;
}

TAPI_NAMESPACE_INTERNAL_END
