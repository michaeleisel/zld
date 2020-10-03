//===- tapi/Core/Platform.cpp - Platform ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements platform specific helper functions.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/Platform.h"
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

Platform mapToSim(Platform platform, bool wantSim) {
  switch (platform) {
  default:
    return platform;
  case Platform::iOS:
    return wantSim ? Platform::iOSSimulator : Platform::iOS;
  case Platform::tvOS:
    return wantSim ? Platform::tvOSSimulator : Platform::tvOS;
  case Platform::watchOS:
    return wantSim ? Platform::watchOSSimulator : Platform::watchOS;
  }
}

Platform mapToPlatform(const Triple &target) {
  switch (target.getOS()) {
  default:
    return Platform::unknown;
  case Triple::MacOSX:
    return Platform::macOS;
  case Triple::IOS:
    if (target.isSimulatorEnvironment())
      return Platform::iOSSimulator;
    if (target.getEnvironment() == Triple::MacABI)
      return Platform::macCatalyst;
    return Platform::iOS;
  case Triple::TvOS:
    return target.isSimulatorEnvironment() ? Platform::tvOSSimulator
                                           : Platform::tvOS;
  case Triple::WatchOS:
    return target.isSimulatorEnvironment() ? Platform::watchOSSimulator
                                           : Platform::watchOS;
  }
}

Platform mapToPlatformFromXBSEnv(StringRef env) {
  return StringSwitch<Platform>(env)
      .Case("ios", Platform::iOS)
      .Case("watch", Platform::watchOS)
      .Case("atv", Platform::tvOS)
      .Case("osx", Platform::macOS)
      .Case("bridgeos", Platform::bridgeOS)
      .Case("ios_sim", Platform::iOSSimulator)
      .Case("watch_sim", Platform::watchOSSimulator)
      .Case("atv_sim", Platform::tvOSSimulator)
      .Default(Platform::unknown);
}

PlatformSet mapToPlatformSet(ArrayRef<Triple> targets) {
  PlatformSet result;
  for (const auto &target : targets)
    result.emplace(mapToPlatform(target));
  return result;
}

StringRef getPlatformName(Platform platform) {
  switch (platform) {
  case Platform::unknown:
    return "unknown";
  case Platform::macOS:
    return "macOS";
  case Platform::iOS:
    return "iOS";
  case Platform::tvOS:
    return "tvOS";
  case Platform::watchOS:
    return "watchOS";
  case Platform::macCatalyst:
    return "macCatalyst";
  case Platform::iOSSimulator:
    return "iOS Simulator";
  case Platform::tvOSSimulator:
    return "tvOS Simulator";
  case Platform::watchOSSimulator:
    return "watchOS Simulator";
  }
}

std::string getOSAndEnvironmentName(Platform platform, std::string version) {
  switch (platform) {
  case Platform::unknown:
    return "darwin" + version;
  case Platform::macOS:
    return "macos" + version;
  case Platform::iOS:
    return "ios" + version;
  case Platform::tvOS:
    return "tvos" + version;
  case Platform::watchOS:
    return "watchos" + version;
  case Platform::bridgeOS:
    return "bridgeos" + version;
  case Platform::macCatalyst:
    return "ios" + version + "-macabi";
  case Platform::iOSSimulator:
    return "ios" + version + "-simulator";
  case Platform::tvOSSimulator:
    return "tvos" + version + "-simulator";
  case Platform::watchOSSimulator:
    return "watchos" + version + "-simulator";
  }
}

raw_ostream &operator<<(raw_ostream &os, Platform platform) {
  os << getPlatformName(platform);
  return os;
}

raw_ostream &operator<<(raw_ostream &os, PlatformSet platforms) {
  os << "[ ";
  unsigned index = 0;
  for (auto platform : platforms) {
    if (index > 0)
      os << ", ";
    os << platform;
    ++index;
  }
  os << " ]";
  return os;
}

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &db,
                                    Platform platform) {
  db.AddString(getPlatformName(platform));
  return db;
}

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &db,
                                    PlatformSet platforms) {
  std::string diagString;
  diagString.append("[ ");
  unsigned index = 0;
  for (auto platform : platforms) {
    if (index > 0)
      diagString.append(", ");
    diagString.append(getPlatformName(platform));
    ++index;
  }
  diagString.append(" ]");
  db.AddString(diagString);
  return db;
}

TAPI_NAMESPACE_INTERNAL_END
