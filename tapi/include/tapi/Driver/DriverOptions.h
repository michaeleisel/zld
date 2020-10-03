//===--- tapi/Driver/DriverOptions.h - Option info & table ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef TAPI_DRIVER_DRIVER_OPTIONS_H
#define TAPI_DRIVER_DRIVER_OPTIONS_H

#include "tapi/Defines.h"

namespace llvm {
namespace opt {
class OptTable;
}
}

TAPI_NAMESPACE_INTERNAL_BEGIN

/// Flags specifically for tapi options. Must not overlap with
/// llvm::opt::DriverFlag.
// clang-format off
enum TapiFlags {
  DriverOption           = 1U <<  4,
  ArchiveOption          = 1U <<  5,
  StubOption             = 1U <<  6,
  InstallAPIOption       = 1U <<  7,
  ReexportOption         = 1U <<  8,
  SDKDBOption            = 1U <<  9,
  GenerateAPITestsOption = 1U << 10,
};
// clang-format on

// Create enum with OPT_xxx values for each option in TAPIOptions.td.
enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM,  \
               HELPTEXT, METAVAR, VALUES)                                      \
  OPT_##ID,
#include "tapi/Driver/TAPIOptions.inc"
  LastOption
#undef OPTION
};

llvm::opt::OptTable *createDriverOptTable();

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_DRIVER_DRIVER_OPTIONS_H
