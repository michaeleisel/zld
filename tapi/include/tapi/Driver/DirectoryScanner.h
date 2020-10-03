//===- tapi/Driver/DirectoryScanner.h - DirectoryScanner --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the directory scanner.
//===----------------------------------------------------------------------===//

#ifndef TAPI_DRIVER_DIRECTORYSCANNER_H
#define TAPI_DRIVER_DIRECTORYSCANNER_H

#include "tapi/Core/Framework.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Core/Registry.h"
#include "tapi/Defines.h"
#include "tapi/Driver/Configuration.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <vector>


TAPI_NAMESPACE_INTERNAL_BEGIN

class DiagnosticsEngine;
class FileManager;
struct Framework;
enum class HeaderType;

/// Directory Scanner mode.
class ScannerMode {
public:
  enum Mode {
    ScanFrameworks,  /// Scanning Framework directory (-F)
    ScanDylibs,      /// Scanning Dylib directory.
    ScanRuntimeRoot, /// Scanning for all binaries in the runtime root.
    ScanPublicSDK,   /// Scanning for all public headers in public SDK.
    ScanInternalSDK, /// Scanning for all headers in internal SDK.
  };

  ScannerMode(const Mode mode) : mode(mode) {}
  Mode getMode() const { return mode; }

  bool scanBinaries() const;
  bool scanBundles() const;
  bool scanHeaders() const;
  bool scanPrivateHeaders() const;
  bool isRootLayout() const;

private:
  Mode mode;
};

class DirectoryScanner {
public:
  DirectoryScanner(FileManager &fm, DiagnosticsEngine &diag,
                   ScannerMode mode = ScannerMode::ScanFrameworks);

  bool scan(StringRef directory);

  // Access scanner internal.
  void setMode(ScannerMode scanMode) { mode = scanMode; }
  void setConfiguration(Configuration *conf) { config = conf; }

  // Get scanner output.
  std::vector<Framework> takeResult();

private:
  // Private helper functions.
  Expected<bool> isDynamicLibrary(StringRef path) const;

  Framework &getOrCreateFramework(StringRef path,
                                  std::vector<Framework> &frameworks) const;

  bool scanDylibDirectory(StringRef directory,
                          std::vector<Framework> &frameworks) const;
  bool scanFrameworksDirectory(std::vector<Framework> &frameworks,
                               StringRef directory) const;
  bool scanSubFrameworksDirectory(std::vector<Framework> &frameworks,
                                  StringRef path) const;
  bool scanFrameworkDirectory(Framework &framework, StringRef path) const;
  bool scanHeaders(Framework &framework, StringRef path, HeaderType type) const;
  bool scanModules(Framework &framework, StringRef path) const;
  bool scanFrameworkVersionsDirectory(Framework &framework,
                                      StringRef path) const;
  bool scanLibraryDirectory(Framework &framework, StringRef path) const;

  bool scanDirectory(StringRef directory);
  bool scanSDKContent(StringRef directory);

private:
  Registry _registry;
  FileManager &_fm;
  DiagnosticsEngine &diag;
  StringRef rootPath;

  ScannerMode mode;
  Configuration *config;
  std::vector<Framework> frameworks;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_DRIVER_DIRECTORYSCANNER_H
