//===- tapi/Core/Target.h - Platform ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the target.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_TARGET_H
#define TAPI_CORE_TARGET_H

#include "tapi/Core/Architecture.h"
#include "tapi/Core/ArchitectureSet.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Core/Platform.h"
#include "tapi/Defines.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Error.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

// This is similar to a llvm Triple, but the triple doesn't have all the
// information we need. For example there is no enum value for x86_64h. The
// only way to get that infrormation is to parse the triple string.
class Target {
public:
  Architecture architecture;
  Platform platform;

  Target() = default;
  Target(Architecture architecture, Platform platform)
      : architecture(architecture), platform(platform) {}
  explicit Target(const llvm::Triple &triple)
      : architecture(mapToArchitecture(triple)),
        platform(mapToPlatform(triple)) {}

  static llvm::Expected<Target> create(StringRef target);

  operator std::string() const;
};

inline bool operator==(const Target &lhs, const Target &rhs) {
  return std::tie(lhs.architecture, lhs.platform) ==
         std::tie(rhs.architecture, rhs.platform);
}

inline bool operator!=(const Target &lhs, const Target &rhs) {
  return std::tie(lhs.architecture, lhs.platform) !=
         std::tie(rhs.architecture, rhs.platform);
}

inline bool operator<(const Target &lhs, const Target &rhs) {
  return std::tie(lhs.architecture, lhs.platform) <
         std::tie(rhs.architecture, rhs.platform);
}

inline bool operator==(const Target &lhs, const Architecture &rhs) {
  return lhs.architecture == rhs;
}

inline bool operator!=(const Target &lhs, const Architecture &rhs) {
  return lhs.architecture != rhs;
}

PlatformSet mapToPlatformSet(ArrayRef<Target> targets);
ArchitectureSet mapToArchitectureSet(ArrayRef<Target> targets);

raw_ostream &operator<<(raw_ostream &os, const Target &target);

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_PLATFORM_H
