//===- tapi/Core/Architecture.cpp - Architecture ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the architecture.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/Architecture.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Defines.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;


TAPI_NAMESPACE_INTERNAL_BEGIN

Architecture getArchType(uint32_t CPUType, uint32_t CPUSubType) {
#define ARCHINFO(arch, type, subtype)                                          \
  if (CPUType == (type) &&                                                     \
      (CPUSubType & ~MachO::CPU_SUBTYPE_MASK) == (subtype))                    \
    return AK_##arch;
#include "tapi/Core/Architecture.def"
#undef ARCHINFO

  return AK_unknown;
}

Architecture getArchType(StringRef name) {
  return StringSwitch<Architecture>(name)
#define ARCHINFO(arch, type, subtype) .Case(#arch, AK_##arch)
#include "tapi/Core/Architecture.def"
#undef ARCHINFO
      .Default(AK_unknown);
}

StringRef getArchName(Architecture arch) {
  switch (arch) {
#define ARCHINFO(arch, type, subtype)                                          \
  case AK_##arch:                                                              \
    return #arch;
#include "tapi/Core/Architecture.def"
#undef ARCHINFO
  case AK_unknown:
    return "unknown";
  }
}

std::pair<uint32_t, uint32_t> getCPUType(Architecture arch) {
  switch (arch) {
#define ARCHINFO(arch, type, subtype)                                          \
  case AK_##arch:                                                              \
    return std::make_pair(type, subtype);
#include "tapi/Core/Architecture.def"
#undef ARCHINFO
  case AK_unknown:
    return std::make_pair(0, 0);
  }
}

Architecture mapToArchitecture(const Triple &target) {
  return getArchType(target.getArchName());
}

raw_ostream &operator<<(raw_ostream &os, Architecture arch) {
  os << getArchName(arch);
  return os;
}

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &db,
                                    Architecture arch) {
  db.AddString(getArchName(arch));
  return db;
}

TAPI_NAMESPACE_INTERNAL_END
