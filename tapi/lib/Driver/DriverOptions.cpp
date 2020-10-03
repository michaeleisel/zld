//===--- DriverOptions.cpp - Driver Options Table -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "tapi/Driver/DriverOptions.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"

using namespace llvm;
using namespace llvm::opt;
using namespace tapi::internal;

/// Create prefix string literals used in TAPIOptions.td.
#define PREFIX(NAME, VALUE) static const char *const NAME[] = VALUE;
#include "tapi/Driver/TAPIOptions.inc"
#undef PREFIX

/// Create table mapping all options defined in TAPIOptions.td.
static const OptTable::Info infoTable[] = {
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM,  \
               HELPTEXT, METAVAR, VALUES)                                      \
  {                                                                            \
    PREFIX, NAME, HELPTEXT, METAVAR, OPT_##ID, Option::KIND##Class, PARAM,     \
        FLAGS, OPT_##GROUP, OPT_##ALIAS, ALIASARGS, VALUES                     \
  }                                                                            \
  ,
#include "tapi/Driver/TAPIOptions.inc"
#undef OPTION
};

namespace {

/// \brief Create OptTable class for parsing actual command line arguments.
class DriverOptTable : public OptTable {
public:
  DriverOptTable() : OptTable(infoTable, array_lengthof(infoTable)) {}
};

} // end anonymous namespace.

OptTable *TAPI_INTERNAL::createDriverOptTable() { return new DriverOptTable(); }
