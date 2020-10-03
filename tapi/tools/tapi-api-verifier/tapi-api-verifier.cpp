//===- tapi-api-verifier/tapi-api-verifier.cpp - APIVerifier ----*- C++ -*-===//
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
/// A tool compares APIs between two targets/frameworks.
///
//===----------------------------------------------------------------------===//
#include "tapi/APIVerifier/APIVerifier.h"
#include "tapi/Config/Version.h"
#include "tapi/Core/APIPrinter.h"
#include "tapi/Core/APIJSONSerializer.h"
#include "tapi/Core/HeaderFile.h"
#include "tapi/Diagnostics/Diagnostics.h"
#include "tapi/Driver/DirectoryScanner.h"
#include "tapi/Driver/HeaderGlob.h"
#include "tapi/Frontend/Frontend.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/Version.inc"
#include "clang/Config/config.h"

using namespace llvm;
using namespace TAPI_INTERNAL;

static cl::OptionCategory tapiCategory("tapi-frontend options");

static cl::opt<clang::InputKind::Language> languageKind(
    "x", cl::init(clang::InputKind::ObjC),
    cl::desc("set input language kind: c, c++, objc, objc++"),
    cl::values(clEnumValN(clang::InputKind::C, "c", "C"),
               clEnumValN(clang::InputKind::CXX, "c++", "C++"),
               clEnumValN(clang::InputKind::ObjC, "objc", "Objective-C"),
               clEnumValN(clang::InputKind::ObjCXX, "objc++", "Objective-C++")),
    cl::cat(tapiCategory));

static cl::opt<std::string> language_std("std",
                                         cl::desc("set the language standard"),
                                         cl::value_desc("<lang>"),
                                         cl::cat(tapiCategory));

static cl::list<std::string> xparser("Xparser",
                                     cl::desc("additional parser option"),
                                     cl::cat(tapiCategory));

static cl::opt<bool> verbose("v", cl::desc("verbose"), cl::cat(tapiCategory));

static cl::opt<std::string> inputFilename(cl::Positional,
                                          cl::desc("<comparsion config file>"),
                                          cl::cat(tapiCategory));

static cl::opt<std::string> whitelist("whitelist",
                                      cl::desc("whitelist YAML file"),
                                      cl::cat(tapiCategory));

static cl::opt<APIVerifierDiagStyle> diagStyle(
    "verifier-diag-style", cl::init(APIVerifierDiagStyle::Warning),
    cl::desc("APIVerifier Diagnostic Style, options: silent, warning, error"),
    cl::values(clEnumValN(APIVerifierDiagStyle::Silent, "silent", "Silent"),
               clEnumValN(APIVerifierDiagStyle::Warning, "warning", "Warning"),
               clEnumValN(APIVerifierDiagStyle::Error, "error", "Error")),
    cl::cat(tapiCategory));

static cl::opt<bool> skipExtern("skip-external-headers",
                                cl::desc("skip external headers"),
                                cl::cat(tapiCategory));

static cl::opt<bool> missingAPI("diag-missing-api",
                                cl::desc("diagnose missing api"),
                                cl::cat(tapiCategory));

static cl::opt<bool> noCascadingDiags("no-cascading-diagnostics",
                                cl::desc("disable cascading errors"),
                                cl::cat(tapiCategory));

static cl::opt<unsigned>
    diagnosticDepth("diag-depth",
                    cl::desc("depth of diagnostics (0 is ignored)"),
                    cl::cat(tapiCategory));

static cl::opt<bool> comparePrivateHeaders(
    "compare-private-header",
    cl::desc("compare private headers instead of public ones"),
    cl::cat(tapiCategory));

namespace {

struct APIComparsionContext {
  std::string target;
  std::string sysroot;
  std::vector<std::string> additionalIncludes;
  std::vector<std::string> additionalFrameworks;
  std::string path;
};

struct APIComparsionConfiguration {
  APIComparsionContext base;
  APIComparsionContext variant;
};

} // end namespace.

// YAML Traits
namespace llvm {
namespace yaml {

template <> struct MappingTraits<APIComparsionContext> {
  static void mapping(IO &io, APIComparsionContext &config) {
    io.mapRequired("target", config.target);
    io.mapRequired("sysroot", config.sysroot);
    io.mapOptional("includes", config.additionalIncludes);
    io.mapOptional("frameworks", config.additionalFrameworks);
    io.mapRequired("path", config.path);
  }
};

template <> struct MappingTraits<APIComparsionConfiguration> {
  static void mapping(IO &io, APIComparsionConfiguration &config) {
    io.mapRequired("base", config.base);
    io.mapRequired("variant", config.variant);
  }
};

} // end namespace yaml
} // end namespace llvm

static std::string getClangResourcesPath(clang::FileManager &fm) {
  // Exists solely for the purpose of lookup of the resource path.
  // This just needs to be some symbol in the binary.
  static int staticSymbol;
  // The driver detects the builtin header path based on the path of the
  // executable.
  auto mainExecutable =
      sys::fs::getMainExecutable("tapi-api-verifier", &staticSymbol);
  StringRef dir = llvm::sys::path::parent_path(mainExecutable);

  // Compute the path to the resource directory.
  auto fileExists = [&fm](StringRef path) {
    llvm::vfs::Status result;
    if (fm.getNoncachedStatValue(path, result))
      return false;
    return result.exists();
  };

  // Try the default tapi path. tapi-api-verifier in installed into /local/bin
  SmallString<PATH_MAX> libDir(dir);
  llvm::sys::path::append(libDir, "..", "..",
                          Twine("lib") + CLANG_LIBDIR_SUFFIX);
  if (!fileExists(libDir))
    return std::string();

  SmallString<PATH_MAX> path(libDir);
  llvm::sys::path::append(path, "tapi", TAPI_MAKE_STRING(TAPI_VERSION));
  if (fileExists(path))
    return path.str();

  // Try the default clang path. This is used by check-tapi.
  path = libDir;
  llvm::sys::path::append(path, "clang", CLANG_VERSION_STRING);
  if (fileExists(path))
    return path.str();

  return std::string();
}

static bool populateHeaderSeq(StringRef path, HeaderSeq &headerFiles,
                              FileManager &fm, DiagnosticsEngine &diag) {
  DirectoryScanner scanner(fm, diag, ScannerMode::ScanFrameworks);

  if (fm.isDirectory(path, /*CacheFailure=*/false)) {
    SmallString<PATH_MAX> normalizedPath(path);
    fm.getVirtualFileSystem()->makeAbsolute(normalizedPath);
    sys::path::remove_dots(normalizedPath, /*remove_dot_dot=*/true);
    if (!scanner.scan(normalizedPath))
      return false;
  } else {
    diag.report(diag::err_no_directory) << path;
    return false;
  }

  auto frameworks = scanner.takeResult();
  if (frameworks.empty()) {
    diag.report(diag::err_no_framework);
    return false;
  }

  if (frameworks.size() > 1) {
    diag.report(diag::err_more_than_one_framework);
    return false;
  }

  auto *framework = &frameworks.back();

  if (!framework->_versions.empty())
    framework = &framework->_versions.back();

  for (const auto &header : framework->_headerFiles) {
    auto *file = fm.getFile(header.fullPath);
    if (!file) {
      diag.report(diag::err_no_such_header_file)
          << header.fullPath << (unsigned)header.type;
      return false;
    }
    headerFiles.emplace_back(header);
  }

  // Check if the framework has an umbrella header and move that to the
  // beginning.
  auto matchAndMarkUmbrella = [](HeaderSeq &array, Regex &regex,
                                 HeaderType type) -> bool {
    auto it = find_if(array, [&regex, type](const HeaderFile &header) {
      return (header.type == type) && regex.match(header.fullPath);
    });

    if (it == array.end())
      return false;

    it->isUmbrellaHeader = true;
    return true;
  };

  auto frameworkName = sys::path::stem(framework->getName());
  {
    auto umbrellaName = "/" + Regex::escape(frameworkName) + "\\.h";
    Regex umbrellaRegex(umbrellaName);
    matchAndMarkUmbrella(headerFiles, umbrellaRegex, HeaderType::Public);
  }

  {
    auto umbrellaName = "/" + Regex::escape(frameworkName) + "[_]?Private\\.h";
    Regex umbrellaRegex(umbrellaName);

    matchAndMarkUmbrella(headerFiles, umbrellaRegex, HeaderType::Private);
  }

  std::stable_sort(headerFiles.begin(), headerFiles.end());
  return true;
}

int main(int argc, const char *argv[]) {
  // Standard set up, so program fails gracefully.
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram stackPrinter(argc, argv);
  llvm_shutdown_obj shutdown;

  if (sys::Process::FixupStandardFileDescriptors())
    return 1;

  cl::HideUnrelatedOptions(tapiCategory);
  cl::ParseCommandLineOptions(argc, argv, "TAPI API Verifier\n");

  if (inputFilename.empty()) {
    cl::PrintHelpMessage();
    return 0;
  }

  // Parse input.
  APIComparsionConfiguration config;
  {
    auto inputBuf = MemoryBuffer::getFile(inputFilename);
    if (!inputBuf) {
      errs() << "cannot open input configuration file: " << inputFilename
             << "\n";
      return -1;
    }
    yaml::Input yin((*inputBuf)->getMemBufferRef());
    yin >> config;
  }

  std::vector<FrontendContext> results;
  FileManager fm((clang::FileSystemOptions()));
  DiagnosticsEngine diag;

  auto parseHeaders = [&](APIComparsionContext &context) {
    FrontendJob job;
    HeaderSeq headers;
    if (!populateHeaderSeq(context.path, headers, fm, diag))
      return false;
    job.target = Triple(context.target);
    job.isysroot = context.sysroot;
    job.language = languageKind;
    job.language_std = language_std;
    job.verbose = verbose;
    job.clangExtraArgs = xparser;
    job.headerFiles = headers;
    job.type = comparePrivateHeaders ? HeaderType::Private : HeaderType::Public;
    job.clangResourcePath = getClangResourcesPath(fm);
    job.systemFrameworkPaths = context.additionalFrameworks;
    job.systemIncludePaths = context.additionalIncludes;
    auto result = runFrontend(job);
    if (!result)
      return false;
    results.emplace_back(std::move(result.getValue()));
    return true;
  };

  if (!parseHeaders(config.base) || !parseHeaders(config.variant))
    return -1;

  APIVerifier apiVerifier(diag);
  if (!whitelist.empty()) {
    auto inputBuf = MemoryBuffer::getFile(whitelist);
    if (!inputBuf) {
      errs() << "cannot open whitelist file: " << whitelist << "\n";
      return -1;
    }

    auto error = apiVerifier.getConfiguration().readConfig(
        (*inputBuf)->getMemBufferRef());
    if (error) {
      errs() << "cannot parse whitelist file: " << toString(std::move(error))
             << "\n";
      return -1;
    }
  }

  apiVerifier.verify(results.front(), results.back(), diagnosticDepth,
                     !skipExtern, diagStyle, missingAPI, noCascadingDiags);

  return 0;
}
