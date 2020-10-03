//===- tapi/Driver/Driver.h - TAPI Driver -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the driver interface for tapi.
//===----------------------------------------------------------------------===//

#ifndef TAPI_DRIVER_DRIVER_H
#define TAPI_DRIVER_DRIVER_H

#include "tapi/Core/LLVM.h"
#include "tapi/Defines.h"
#include "tapi/Diagnostics/Diagnostics.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"

namespace llvm {
raw_ostream &errs();
}

TAPI_NAMESPACE_INTERNAL_BEGIN

class Options;

class Driver {
private:
  /// \brief Create a diagnostics engine that will emit errors to a stream
  static IntrusiveRefCntPtr<DiagnosticsEngine>
  createDiagnosticsEngine(raw_ostream &errorStream = llvm::errs());

  static bool run(DiagnosticsEngine &diag, Options &options);

public:
  /// \brief Parses the command line options and performs the requested action.
  static bool run(llvm::ArrayRef<const char *> args);

  Driver() = delete;

  class Archive {
  public:
    /// \brief Run tapi with the provided arguments.
    static bool run(DiagnosticsEngine &diag, Options &opts);

    Archive() = delete;
  };

  class Stub {
  public:
    /// \brief Run tapi with the provided arguments.
    static bool run(DiagnosticsEngine &diag, Options &opts);

    Stub() = delete;
  };

  class InstallAPI {
  public:
    /// \brief Run tapi with the provided arguments.
    static bool run(DiagnosticsEngine &diag, Options &opts);

    InstallAPI() = delete;
  };

  class Reexport {
  public:
    /// \brief Run tapi with the provided arguments.
    static bool run(DiagnosticsEngine &diag, Options &opts);

    Reexport() = delete;
  };


  class GenerateAPITests {
  public:
    /// \brief Run tapi with the provided arguments.
    static bool run(DiagnosticsEngine &diag, Options &opts);

    GenerateAPITests() = delete;
  };
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_DRIVER_DRIVER_H
