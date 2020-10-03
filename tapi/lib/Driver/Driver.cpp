//===- lib/Driver/Driver.cpp - TAPI Driver ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the driver for the tapi tool.
///
/// Currently the driver only provides the basic framework for option parsing.
///
//===----------------------------------------------------------------------===//

#include "tapi/Driver/Driver.h"
#include "tapi/Config/Version.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Driver/Options.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

IntrusiveRefCntPtr<DiagnosticsEngine>
Driver::createDiagnosticsEngine(raw_ostream &errorStream) {
  return new DiagnosticsEngine(errorStream);
}

bool Driver::run(DiagnosticsEngine &diag, Options &options) {
  // Handle -version.
  if (options.driverOptions.printVersion) {
    outs() << getTAPIFullVersion() << "\n";
    return true;
  }

  return true;
}

/// Parses the command line options and performs the requested action.
bool Driver::run(ArrayRef<const char *> args) {
  auto diag = createDiagnosticsEngine();
  // Parse command line options using TAPIOptions.td.

  Options options(*diag, args);

  // Check if there have been errors during the option parsing.
  if (diag->hasErrorOccurred())
    return false;

  if (options.driverOptions.printHelp) {
    options.printHelp();
    return true;
  }

  switch (options.command) {
  case TAPICommand::Driver:
    return Driver::run(*diag, options);
  case TAPICommand::Archive:
    return Archive::run(*diag, options);
  case TAPICommand::Stubify:
    return Stub::run(*diag, options);
  case TAPICommand::InstallAPI:
    return InstallAPI::run(*diag, options);
  case TAPICommand::Reexport:
    return Reexport::run(*diag, options);
  case TAPICommand::GenerateAPITests:
    return GenerateAPITests::run(*diag, options);
  }
  llvm_unreachable("invalid/unknown driver command");
}

TAPI_NAMESPACE_INTERNAL_END
