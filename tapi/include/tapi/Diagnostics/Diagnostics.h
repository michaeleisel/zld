//===--- Diagnostic.h - TAPI Diagnostics Handling ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_DIAGNOSTIC_H
#define TAPI_CORE_DIAGNOSTIC_H

#include "tapi/Core/LLVM.h"
#include "tapi/Defines.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"

namespace llvm {
raw_ostream &errs();
}

TAPI_NAMESPACE_INTERNAL_BEGIN

namespace diag {
using Severity = clang::diag::Severity;

enum {
  DIAG_START_TAPI = clang::diag::DIAG_UPPER_LIMIT,
#define DIAG(ENUM, FLAGS, DEFAULT_MAPPING, DESC, GROUP, SFINAE, CATEGORY,      \
             NOWERROR, SHOWINSYSHEADER)                                        \
  ENUM,
#include "tapi/Diagnostics/DiagnosticTAPIKinds.inc"
#undef DIAG
};
} // end namespace diag

/// \brief The Tapi diagnostic engine.
///
/// This wraps the Clang diagnostic engine to augment it with additonal Tapi
/// specific diagnostics.
class DiagnosticsEngine : public llvm::RefCountedBase<DiagnosticsEngine> {
public:
  explicit DiagnosticsEngine(raw_ostream &errorStream = llvm::errs());
  explicit DiagnosticsEngine(clang::DiagnosticConsumer *);
  DiagnosticsEngine(const DiagnosticsEngine &) = delete;
  ~DiagnosticsEngine();
  void operator=(const DiagnosticsEngine &) = delete;

  clang::DiagnosticBuilder report(unsigned diagID) {
    return report(clang::SourceLocation(), diagID);
  }
  clang::DiagnosticBuilder report(clang::SourceLocation loc, unsigned diagID);
  void setWarningsAsErrors(bool value) { warningsAsErrors = value; }
  void setErrorLimit(unsigned value) { diag->setErrorLimit(value); }
  bool hasErrorOccurred() const { return diag->hasErrorOccurred(); }

  void setupDiagnosticsFile(StringRef output);

  void setSourceManager(clang::SourceManager *sourceMgr) {
    diag->setSourceManager(sourceMgr);
  }

  clang::SourceManager &getSourceManager() const {
    return diag->getSourceManager();
  }

  void notePriorDiagnosticFrom(DiagnosticsEngine &diag);

  clang::DiagnosticConsumer *getClient() { return diag->getClient(); }

  using ArgToStringFnTy =
      void (*)(clang::DiagnosticsEngine::ArgumentKind, intptr_t, StringRef,
               StringRef, ArrayRef<clang::DiagnosticsEngine::ArgumentValue>,
               SmallVectorImpl<char> &, void *Cookie, ArrayRef<intptr_t>);
  void SetArgToStringFn(ArgToStringFnTy fn, void *cookie) {
    diag->SetArgToStringFn(fn, cookie);
  }

  void setDiagLevel(unsigned diagID, clang::DiagnosticIDs::Level level);

  void ignoreDiagnotic(unsigned diagID) {
    setDiagLevel(diagID, clang::DiagnosticIDs::Ignored);
  }

  void setDiagnosticAsError(unsigned diagID) {
    setDiagLevel(diagID, clang::DiagnosticIDs::Error);
  }

  clang::DiagnosticIDs::Level getDiagnosticLevel(unsigned diagID);

private:
  IntrusiveRefCntPtr<clang::DiagnosticOptions> diagOpts;
  IntrusiveRefCntPtr<clang::DiagnosticsEngine> diag;
  clang::LangOptions langOpts;
  bool warningsAsErrors = false;
  llvm::DenseMap<unsigned, clang::DiagnosticIDs::Level> diagLevelMap;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_DIAGNOSTIC_H
