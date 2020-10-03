//===- lib/Diagnostics/Diagnostics.cpp - TAPI Diagnostics -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the diagnostic handling for tapi.
///
//===----------------------------------------------------------------------===//

#include "tapi/Diagnostics/Diagnostics.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Defines.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Frontend/ChainedDiagnosticConsumer.h"
#include "clang/Frontend/LogDiagnosticPrinter.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/raw_ostream.h"

namespace {

// Diagnostic classes.
enum {
  CLASS_NOTE       = 0x01,
  CLASS_REMARK     = 0x02,
  CLASS_WARNING    = 0x03,
  CLASS_EXTENSION  = 0x04,
  CLASS_ERROR      = 0x05
};

struct DiagInfoRec {
  const char *DescriptionStr;
  uint16_t id;
  uint16_t DescriptionLen;
  uint8_t Class;
  uint8_t DefaultSeverity;

  llvm::StringRef getDescription() const {
    return {DescriptionStr, DescriptionLen};
  }

  bool operator<(const DiagInfoRec &rhs) const { return id < rhs.id; }
};

} // end anonymous namespace.

TAPI_NAMESPACE_INTERNAL_BEGIN

static constexpr DiagInfoRec diagInfo[] = {
#define DIAG(ENUM, CLASS, DEFAULT_SEVERITY, DESC, GROUP, SFINAE, NOWERROR,     \
             SHOWINSYSHEADER, CATEGORY)                                        \
  {DESC, diag::ENUM, sizeof(DESC) - 1, CLASS, DEFAULT_SEVERITY},
#include "tapi/Diagnostics/DiagnosticTAPIKinds.inc"
#undef DIAG
};

static constexpr unsigned diagInfoSize = llvm::array_lengthof(diagInfo);

static clang::DiagnosticOptions *createDiagnosticsEngineOpts() {
  static bool hasColors = llvm::sys::Process::StandardErrHasColors();

  auto *diagOpts = new clang::DiagnosticOptions();
  diagOpts->ShowColors = static_cast<unsigned int>(hasColors);
  return diagOpts;
}

DiagnosticsEngine::DiagnosticsEngine(raw_ostream &errorStream) {
  diagOpts = createDiagnosticsEngineOpts();
  diag = new clang::DiagnosticsEngine(
      new clang::DiagnosticIDs(), diagOpts.get(),
      new clang::TextDiagnosticPrinter(errorStream, diagOpts.get()));
  assert(std::is_sorted(std::begin(diagInfo), std::end(diagInfo)) &&
         "diagnostic array should be sorted");
  diag->getClient()->BeginSourceFile(langOpts);
}

DiagnosticsEngine::DiagnosticsEngine(clang::DiagnosticConsumer *client) {
  diagOpts = createDiagnosticsEngineOpts();
  diag = new clang::DiagnosticsEngine(new clang::DiagnosticIDs(),
                                      diagOpts.get(), client);
  assert(std::is_sorted(std::begin(diagInfo), std::end(diagInfo)) &&
         "diagnostic array should be sorted");
  diag->getClient()->BeginSourceFile(langOpts);
}

DiagnosticsEngine::~DiagnosticsEngine() {
  diag->getClient()->EndSourceFile();
}

void DiagnosticsEngine::notePriorDiagnosticFrom(DiagnosticsEngine &diag2) {
  diag->notePriorDiagnosticFrom(*diag2.diag);
}

clang::DiagnosticIDs::Level
DiagnosticsEngine::getDiagnosticLevel(unsigned diagID) {
  // lookup the diagnostic descripton from the Tapi DiagInfo.
  auto index = diagID - clang::diag::DIAG_UPPER_LIMIT - 1;
  assert(index < diagInfoSize && "invalid Tapi DiagInfo index");
  auto &record = diagInfo[index];

  auto mapEntry = diagLevelMap.find(diagID);
  if (mapEntry != diagLevelMap.end())
    return mapEntry->second;

  if (record.Class == CLASS_NOTE)
    return clang::DiagnosticIDs::Note;

  auto level = static_cast<clang::DiagnosticIDs::Level>(record.DefaultSeverity);
  if (level == clang::DiagnosticIDs::Warning && warningsAsErrors)
    return clang::DiagnosticIDs::Error;

  return level;
}

void DiagnosticsEngine::setDiagLevel(unsigned diagID,
    clang::DiagnosticIDs::Level level) {
  auto entry = diagLevelMap.insert({diagID, level});
  if (!entry.second)
    entry.first->second = level;
}

clang::DiagnosticBuilder DiagnosticsEngine::report(clang::SourceLocation loc,
                                                   unsigned diagID) {
  // pass through all clang diagnostics.
  if (diagID <= clang::diag::DIAG_UPPER_LIMIT)
    return diag->Report(loc, diagID);

  // lookup the diagnostic descripton from the Tapi DiagInfo.
  auto index = diagID - clang::diag::DIAG_UPPER_LIMIT - 1;
  assert(index < diagInfoSize && "invalid Tapi DiagInfo index");
  (void)diagInfoSize;
  auto &record = diagInfo[index];
  auto diagIDs = diag->getDiagnosticIDs();
  auto level = getDiagnosticLevel(diagID);
  unsigned newID = diagIDs->getCustomDiagID(level, record.getDescription());
  return diag->Report(loc, newID);
}

void DiagnosticsEngine::setupDiagnosticsFile(StringRef output) {
  std::error_code ec;
  std::unique_ptr<raw_ostream> streamOwner;
  raw_ostream *os = &llvm::errs();
  if (output != "-") {
    // Create the output stream.
    auto fileOS = llvm::make_unique<llvm::raw_fd_ostream>(
        output, ec, llvm::sys::fs::F_Append | llvm::sys::fs::F_Text);
    if (ec) {
      report(diag::err_cannot_open_file)
          << output << ec.message();
    } else {
      fileOS->SetUnbuffered();
      os = fileOS.get();
      streamOwner = std::move(fileOS);
    }
  }
  diagOpts->DiagnosticLogFile = output.str();

  // Chain in the diagnostic client which will log the diagnostics.
  auto Logger = llvm::make_unique<clang::LogDiagnosticPrinter>(
      *os, diagOpts.get(), std::move(streamOwner));
  assert(diag->ownsClient());
  diag->setClient(new clang::ChainedDiagnosticConsumer(diag->takeClient(),
                                                       std::move(Logger)));
}

TAPI_NAMESPACE_INTERNAL_END
