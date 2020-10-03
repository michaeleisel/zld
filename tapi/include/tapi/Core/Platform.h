//===- tapi/Core/Platform.h - Platform --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the platform enum.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_PLATFORM_H
#define TAPI_CORE_PLATFORM_H

#include "tapi/Core/LLVM.h"
#include "tapi/Defines.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/BinaryFormat/MachO.h"
#include <set>
#include <string>

TAPI_NAMESPACE_INTERNAL_BEGIN

using namespace llvm::MachO;
enum class Platform : uint8_t {
  unknown,
  macOS = PLATFORM_MACOS,
  iOS = PLATFORM_IOS,
  tvOS = PLATFORM_TVOS,
  watchOS = PLATFORM_WATCHOS,
  bridgeOS = PLATFORM_BRIDGEOS,
  macCatalyst = PLATFORM_MACCATALYST,
  iOSSimulator = PLATFORM_IOSSIMULATOR,
  tvOSSimulator = PLATFORM_TVOSSIMULATOR,
  watchOSSimulator = PLATFORM_WATCHOSSIMULATOR,
};

using PlatformSet = std::set<Platform>;

Platform mapToSim(Platform platform, bool wantSim);
Platform mapToPlatform(const llvm::Triple &target);
PlatformSet mapToPlatformSet(ArrayRef<llvm::Triple> targets);
StringRef getPlatformName(Platform platform);
std::string getOSAndEnvironmentName(Platform platform,
                                    std::string version = "");
Platform mapToPlatformFromXBSEnv(StringRef env);

raw_ostream &operator<<(raw_ostream &os, Platform platform);
raw_ostream &operator<<(raw_ostream &os, PlatformSet platforms);

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &db,
                                    Platform platform);
const DiagnosticBuilder &operator<<(const DiagnosticBuilder &db,
                                    PlatformSet platforms);

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_PLATFORM_H
