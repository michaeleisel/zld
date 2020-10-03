//===- GenerateAPITestsDriver.cpp - TAPI API Test Generator -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the API test generator.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/APIVisitor.h"
#include "tapi/Core/Architecture.h"
#include "tapi/Core/Framework.h"
#include "tapi/Core/HeaderFile.h"
#include "tapi/Defines.h"
#include "tapi/Diagnostics/Diagnostics.h"
#include "tapi/Driver/DirectoryScanner.h"
#include "tapi/Driver/Driver.h"
#include "tapi/Driver/Options.h"
#include "tapi/Frontend/Frontend.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace llvm;
using namespace clang;

TAPI_NAMESPACE_INTERNAL_BEGIN

class JSONEmitter : public APIVisitor {
public:
  JSONEmitter(raw_ostream &os, int n = 0) : os(os), n(n) {}

  inline void emitAvailability(const AvailabilityInfo &availability) {
    os.indent(n + 4) << "\"availability\": {\n";
    os.indent(n + 8) << "\"introduced\": \"" << availability._introduced << "\",\n";
    if (availability._obsoleted.empty())
      os.indent(n + 8) << "\"obsoleted\": null,\n";
    else
      os.indent(n + 8) << "\"obsoleted\": \"" << availability._obsoleted << "\",\n";
    os.indent(n + 8) << "\"unavailable\": "
                     << (availability._unavailable ? "true" : "false") << "\n";
    os.indent(n + 4) << "}\n";
  }

  void visitGlobal(const GlobalRecord &record) override {
    // Skip non exported symbol.
    if (record.linkage != APILinkage::Exported)
      return;

    if (!firstSymbol)
      os << ",\n";
    firstSymbol = false;

    auto name = record.name.drop_front(1);
    os.indent(n + 0) << "{\n";
    os.indent(n + 4) << "\"type\": "
                     << ((record.kind == GVKind::Variable) ? "\"variable\""
                                                           : "\"function\"")
                     << ",\n";
    os.indent(n + 4) << "\"name\": \"" << name << "\",\n";

    StringRef file = record.loc.getFilename();
    auto pos = file.rfind("Headers/");
    if (pos == StringRef::npos)
      llvm_unreachable("unexpected paht for a framework header");
    auto header = file.substr(pos + sizeof("Headers/")-1);
    os.indent(n + 4) << "\"header_file\": \"" << header << "\",\n";

    emitAvailability(record.availability);
    os.indent(n + 0) << "}";
  }

  void visitObjCInterface(const ObjCInterfaceRecord &record) override {
    if (!firstSymbol) {
      os << ",\n";
    }
    firstSymbol = false;

    os.indent(n + 0) << "{\n";
    os.indent(n + 4) << "\"type\": \"objectivec_class\",\n";
    os.indent(n + 4) << "\"name\": \"" << record.name << "\",\n";

    StringRef file = record.loc.getFilename();
    auto pos = file.rfind("Headers/");
    if (pos == StringRef::npos)
      llvm_unreachable("unexpected paht for a framework header");
    auto header = file.substr(pos + sizeof("Headers/")-1);
    os.indent(n + 4) << "\"header_file\": \"" << header << "\",\n";

    emitAvailability(record.availability);
    os.indent(n + 0) << "}";
  }

private:
  raw_ostream &os;
  int n;
  bool firstSymbol{true};
};

static bool emitJSON(const Framework &framework, StringRef isysroot, const Triple &target,
                     raw_ostream &os, unsigned n,
                     std::string umbrellaFramework = std::string()) {
  static bool firstFramework = true;

  for (auto &version : framework._versions) {
    if (!emitJSON(version, isysroot, target, os, n, umbrellaFramework))
      return false;
  }

  auto frameworkName = sys::path::stem(framework.getName());
  if (umbrellaFramework.empty())
    umbrellaFramework = frameworkName;
  for (auto &sub : framework._subFrameworks) {
    if (!emitJSON(sub, isysroot, target, os, n, umbrellaFramework))
      return false;
  }

  if (framework._headerFiles.empty())
    return true;

  auto symbols = find_if(
      framework._frontendResults,
      [&target](const FrontendContext &ctx) { return ctx.target == target; });
  if (symbols == framework._frontendResults.end())
    return true;

  if (firstFramework)
    firstFramework = false;
  else
    os << ",\n";

  unsigned major, minor, patch;
  target.getOSVersion(major, minor, patch);
  auto version = PackedVersion(major, minor, patch);

  os.indent(n + 0) << "{\n";
  os.indent(n + 4) << "\"type\": \"framework\",\n";
  os.indent(n + 4) << "\"name\": \"" << frameworkName << "\",\n";
  os.indent(n + 4) << "\"architecture\": \"" << target.getArchName() << "\",\n";

  auto path = framework.getPath();
  while (!path.empty()) {
    if (path.endswith(".framework"))
      break;
    path = sys::path::parent_path(path);
  }
  os.indent(n + 4) << "\"location\": \"" << path << "\",\n";
  auto umbrella = find_if(framework._headerFiles, [](const HeaderFile &file) {
    return file.isUmbrellaHeader;
  });
  if (umbrella != framework._headerFiles.end())
    os.indent(n + 4) << "\"umbrella_header\": \"" << umbrella->relativePath << "\",\n";
  if (umbrellaFramework != frameworkName)
    os.indent(n + 4) << "\"umbrella_framework\": \"" << umbrellaFramework << "\",\n";
  os.indent(n + 4) << "\"platform\": \"" << target.getOSName().take_while([](char c) { return isalpha(c); }) << "\",\n";
  os.indent(n + 4) << "\"os_version\": \"" << version << "\",\n";
  os.indent(n + 4) << "\"apis\": [\n";

  JSONEmitter emitter(os, n + 8);
  symbols->visit(emitter);

  os.indent(n + 0) << "\n";
  os.indent(n + 4) << "]\n";
  os.indent(n + 0) << "}";

  return true;
}

static bool emitJSON(const std::vector<Framework> &frameworks,
                     StringRef isysroot, const Triple &target, raw_ostream &os,
                     unsigned n) {
  for (const auto &framework : frameworks)
    if (!emitJSON(framework, isysroot, target, os, n))
      return false;

  return true;
}

static bool parseFramework(Framework &framework, Options &opts,
                           DiagnosticsEngine &diag) {
  for (auto &sub : framework._subFrameworks)
    if (!parseFramework(sub, opts, diag))
      return false;

  for (auto &version : framework._versions)
    if (!parseFramework(version, opts, diag))
      return false;

  if (framework._headerFiles.empty())
    return true;

  outs() << "Parsing " << framework.getName() << "\n";
  // Setup the header scanning job.
  FrontendJob job;
  job.language = opts.frontendOptions.language;
  job.language_std = opts.frontendOptions.language_std;
  job.isysroot = opts.frontendOptions.isysroot;
  job.macros = opts.frontendOptions.macros;
  job.frameworkPaths = opts.frontendOptions.frameworkPaths;
  job.includePaths = opts.frontendOptions.includePaths;
  job.clangExtraArgs = opts.frontendOptions.clangExtraArgs;
  job.type = HeaderType::Public;
  job.clangResourcePath = opts.frontendOptions.clangResourcePath;

  // Create a sorted list of framework headers.
  HeaderSeq &headerFiles = framework._headerFiles;

  // Check if the framework has an umbrella header and move that to the
  // beginning.
  auto matchUmbrellaAndMoveToTop = [](HeaderSeq &array, Regex &regex,
                                      HeaderType type) -> bool {
    auto it = find_if(array, [&regex, type](const HeaderFile &header) {
      return (header.type == type) && regex.match(header.fullPath);
    });

    if (it == array.end())
      return false;

    it->isUmbrellaHeader = true;
    std::rotate(array.begin(), it, std::next(it));
    return true;
  };
  auto frameworkName = sys::path::stem(framework.getName());
  auto umbrellaName = "/" + Regex::escape(frameworkName) + "\\.h";
  Regex umbrellaRegex(umbrellaName);
  matchUmbrellaAndMoveToTop(headerFiles, umbrellaRegex, HeaderType::Public);

  sort(headerFiles);
  job.headerFiles = headerFiles;

  // Add the current framework directory as a system framework directory. This
  // will prevent it from being droped from the top of the list if there is
  // a matching system framework include path.
  job.frameworkPaths.insert(job.frameworkPaths.begin(),
                            sys::path::parent_path(framework.getPath()));

  for (auto &target : opts.frontendOptions.targets) {
    job.target = target;
    auto result = runFrontend(job);
    if (!result)
      return false;
    framework._frontendResults.emplace_back(std::move(result.getValue()));
  }

  return true;
}

/// \brief Scan the directory for public headers and generate API tests.
bool Driver::GenerateAPITests::run(DiagnosticsEngine &diag, Options &opts) {
  diag.setErrorLimit(opts.diagnosticsOptions.errorLimit);

  // Handle targets.
  if (opts.frontendOptions.targets.empty()) {
    diag.report(diag::err_no_target);
    return false;
  }

  if (opts.driverOptions.inputs.empty()) {
    diag.report(clang::diag::err_drv_no_input_files);
    return false;
  }

  if (opts.driverOptions.inputs.size() != 1) {
    diag.report(diag::err_expected_one_input_file);
    return false;
  }

  // Set default language option.
  if (opts.frontendOptions.language == clang::InputKind::Unknown)
    opts.frontendOptions.language = clang::InputKind::ObjC;

  //
  // Scan through the directories and create a list of all found frameworks.
  //
  DirectoryScanner scanner(opts.getFileManager(), diag);
  auto &path = opts.driverOptions.inputs.back();
  if (!opts.getFileManager().isDirectory(path)) {
    diag.report(diag::err_no_directory) << path;
    return false;
  }

  if (!scanner.scan(path))
    return false;

  auto frameworks = scanner.takeResult();
  if (frameworks.empty()) {
    diag.report(diag::err_no_framework);
    return false;
  }

  sort(frameworks, [](const Framework &lhs, const Framework &rhs) {
    return lhs.getName() < rhs.getName();
  });

  auto last = std::unique(frameworks.begin(), frameworks.end(),
                          [](const Framework &lhs, const Framework &rhs) {
                            return lhs._baseDirectory == rhs._baseDirectory;
                          });
  frameworks.erase(last, frameworks.end());

  for (auto &framework : frameworks)
    if (!parseFramework(framework, opts, diag))
      return false;

  std::error_code ec;
  raw_fd_ostream os(opts.driverOptions.outputPath, ec,
                    sys::fs::OpenFlags::F_None);
  if (ec) {
    errs() << "error: " << ec.message() << ": " << opts.driverOptions.outputPath
           << "\n";
    return false;
  }
  os.indent(0) << "[\n";

  for (const auto &target : opts.frontendOptions.targets)
    if (!emitJSON(frameworks, opts.frontendOptions.isysroot, target, os,
                  /*n=*/4))
      return false;

  os.indent(0) << "\n]\n";
  os.close();
  if (ec) {
    errs() << "error: " << ec.message() << ": " << opts.driverOptions.outputPath
           << "\n";
    return false;
  }

  return true;
}

TAPI_NAMESPACE_INTERNAL_END
