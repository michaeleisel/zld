//===- tools/tapi/tapi.cpp - TAPI Tool --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// The tapi tool is a thin wrapper around the tapi driver.
///
//===----------------------------------------------------------------------===//

#include "tapi/Driver/Driver.h"
#include "tapi/Driver/Snapshot.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"

using namespace tapi::internal;

static void HandleSnapshotEmission(void * /*unused*/) {
  globalSnapshot->writeSnapshot();
}

int main(int argc, const char *argv[]) {
  // Standard set up, so program fails gracefully.
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  llvm::PrettyStackTraceProgram stackPrinter(argc, argv);
  llvm::llvm_shutdown_obj shutdown;
  llvm::sys::AddSignalHandler(HandleSnapshotEmission, nullptr);

  if (llvm::sys::Process::FixupStandardFileDescriptors())
    return 1;

  return Driver::run(llvm::makeArrayRef(argv, argc)) ? 0 : 1;
}
