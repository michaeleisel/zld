//===- lib/Driver/InstallAPIDriver.cpp - TAPI Driver ------------*- C++ -*-===//
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
//===----------------------------------------------------------------------===//

#include "API2XPIConverter.h"
#include "tapi/APIVerifier/APIVerifier.h"
#include "tapi/Core/APIPrinter.h"
#include "tapi/Core/FileListReader.h"
#include "tapi/Core/HeaderFile.h"
#include "tapi/Core/InterfaceFile.h"
#include "tapi/Core/InterfaceFileManager.h"
#include "tapi/Core/Path.h"
#include "tapi/Core/Registry.h"
#include "tapi/Core/Utils.h"
#include "tapi/Core/XPI.h"
#include "tapi/Defines.h"
#include "tapi/Diagnostics/Diagnostics.h"
#include "tapi/Driver/DirectoryScanner.h"
#include "tapi/Driver/Driver.h"
#include "tapi/Driver/HeaderGlob.h"
#include "tapi/Driver/Options.h"
#include "tapi/Driver/Snapshot.h"
#include "tapi/Driver/StatRecorder.h"
#include "tapi/Frontend/Frontend.h"
#include "tapi/LinkerInterfaceFile.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <string>

using namespace llvm;
using namespace llvm::opt;
using namespace clang;

TAPI_NAMESPACE_INTERNAL_BEGIN

static bool verifySymbols(const InterfaceFile *apiFile,
                          const InterfaceFile *dylibFile,
                          DiagnosticsEngine &diag,
                          VerificationMode verificationMode, bool demangle) {
  diag.setWarningsAsErrors(verificationMode == VerificationMode::Pedantic);

  auto xpiCmp = [](const XPI *lhs, const XPI *rhs) {
    if (lhs->getKind() < rhs->getKind())
      return true;
    if (rhs->getKind() < lhs->getKind())
      return false;

    return lhs->getName() < rhs->getName();
  };

  std::vector<const XPI *> symbols;
  for (const auto *symbol : apiFile->symbols())
    symbols.emplace_back(symbol);
  sort(symbols, xpiCmp);

  for (const auto *hsymbol : symbols) {
    auto dsymbol = dylibFile->contains(hsymbol->getKind(), hsymbol->getName());

    if (verificationMode != VerificationMode::ErrorsOnly) {
      if (hsymbol->isUnavailable() && !hsymbol->isObsolete() && dsymbol)
        diag.report(diag::warn_symbol_unavailable)
            << hsymbol->getAnnotatedName(demangle);
    }

    if (hsymbol->isUnavailable())
      continue;

    if (!dsymbol) {
      if (hsymbol->isObsolete())
        continue;

      diag.report(diag::err_library_missing_symbol)
          << hsymbol->getAnnotatedName(demangle);
      continue;
    }

    if (hsymbol->isThreadLocalValue() != (*dsymbol)->isThreadLocalValue()) {
      if (hsymbol->isThreadLocalValue())
        diag.report(diag::err_header_symbol_tlv_mismatch)
            << hsymbol->getAnnotatedName(demangle)
            << (*dsymbol)->getAnnotatedName(demangle);
      else
        diag.report(diag::err_dylib_symbol_tlv_mismatch)
            << (*dsymbol)->getAnnotatedName(demangle)
            << hsymbol->getAnnotatedName(demangle);
    }

    if (hsymbol->isWeakDefined() != (*dsymbol)->isWeakDefined()) {
      if (hsymbol->isWeakDefined())
        diag.report(diag::err_header_symbol_weak_mismatch)
            << hsymbol->getAnnotatedName(demangle)
            << (*dsymbol)->getAnnotatedName(demangle);
      else
        diag.report(diag::err_dylib_symbol_weak_mismatch)
            << (*dsymbol)->getAnnotatedName(demangle)
            << hsymbol->getAnnotatedName(demangle);
    }

    if (hsymbol->getArchitectures() == (*dsymbol)->getArchitectures())
      continue;

    diag.report(diag::err_availability_mismatch)
        << hsymbol->getAnnotatedName(demangle) << hsymbol->getArchitectures()
        << (*dsymbol)->getArchitectures();
  }

  // Check for all special linker symbols. They can affect the runtime behavior
  // and are always required to match even for ErrorsOnly mode.
  symbols.clear();
  for (const auto *symbol : dylibFile->exports())
    symbols.emplace_back(symbol);
  sort(symbols, xpiCmp);
  for (const auto *dsymbol : symbols) {
    // Skip normal symbols. We only care about special linker symbols here.
    if (!dsymbol->getName().startswith("$ld$"))
      continue;

    if (apiFile->contains(dsymbol->getKind(), dsymbol->getName()))
      continue;

    diag.report(diag::err_header_symbol_missing)
        << dsymbol->getAnnotatedName(demangle);
  }

  if (verificationMode == VerificationMode::ErrorsOnly)
    return !diag.hasErrorOccurred();

  for (const auto *dsymbol : symbols) {
    // Skip special linker symbols. We already checked them.
    if (dsymbol->getName().startswith("$ld$"))
      continue;

    // Ignore Swift symbols.
    if (dsymbol->getName().startswith("_$s") ||
        dsymbol->getName().startswith("_$S"))
      continue;

    if (apiFile->contains(dsymbol->getKind(), dsymbol->getName()))
      continue;

    // The existence of weak-defined RTTI can not always be inferred from the
    // header files, because they can be generated as part of an implementation
    // file.
    // We do not warn about weak-defined RTTI, because this doesn't affect
    // linking and can be ignored.
    if (dsymbol->isWeakDefined() && (dsymbol->getName().startswith("__ZTI") ||
                                     dsymbol->getName().startswith("__ZTS")))
      continue;

    // Do not warn about fragile ObjC classes. Even hidden classes are exported
    // and are required to be exported. They are not needed for linking, so we
    // can ignore them.
    if (dsymbol->getKind() == XPIKind::ObjectiveCClass &&
        dsymbol->getArchitectures() == AK_i386)
      continue;

    diag.report(diag::warn_header_symbol_missing)
        << dsymbol->getAnnotatedName(demangle);
  }

  return !diag.hasErrorOccurred();
}

static bool verifyFramework(const InterfaceFile *apiFile,
                            const InterfaceFile *dylibFile,
                            DiagnosticsEngine &diag,
                            VerificationMode verificationMode, bool demangle,
                            bool autoZippered) {
  if (apiFile->getPlatforms() != dylibFile->getPlatforms()) {
    if (autoZippered)
      diag.report(diag::warn_platform_mismatch)
          << apiFile->getPlatforms() << dylibFile->getPlatforms();
    else {
      diag.report(diag::err_platform_mismatch)
          << apiFile->getPlatforms() << dylibFile->getPlatforms();
      return false;
    }
  }

  if (apiFile->getArchitectures() != dylibFile->getArchitectures()) {
    diag.report(diag::err_architecture_mismatch)
        << apiFile->getArchitectures() << dylibFile->getArchitectures();
    return false;
  }

  if (apiFile->getInstallName() != dylibFile->getInstallName()) {
    diag.report(diag::err_install_name_mismatch)
        << apiFile->getInstallName() << dylibFile->getInstallName();
    return false;
  }

  if (apiFile->getCurrentVersion() != dylibFile->getCurrentVersion()) {
    diag.report(diag::err_current_version_mismatch)
        << apiFile->getCurrentVersion() << dylibFile->getCurrentVersion();
    return false;
  }

  if (apiFile->getCompatibilityVersion() !=
      dylibFile->getCompatibilityVersion()) {
    diag.report(diag::err_compatibility_version_mismatch)
        << apiFile->getCompatibilityVersion()
        << dylibFile->getCompatibilityVersion();
    return false;
  }

  if (apiFile->isApplicationExtensionSafe() !=
      dylibFile->isApplicationExtensionSafe()) {
    diag.report(diag::err_appextension_safe_mismatch)
        << (apiFile->isApplicationExtensionSafe() ? "true" : "false")
        << (dylibFile->isApplicationExtensionSafe() ? "true" : "false");
    return false;
  }

  auto compareInterfaceFileRef = [&](const std::vector<InterfaceFileRef> &lhs,
                                     const std::vector<InterfaceFileRef> &rhs,
                                     unsigned diagID_missing,
                                     unsigned diagID_mismatch) {
    if (lhs.size() != rhs.size() ||
        !std::equal(
            lhs.begin(), lhs.end(), rhs.begin(),
            [](const InterfaceFileRef &lhs, const InterfaceFileRef &rhs) {
              return (lhs.getInstallName() == rhs.getInstallName()) &&
                     (lhs.getArchitectures() == rhs.getArchitectures());
            })) {
      for (const auto &ref1 : lhs) {
        auto it = find_if(rhs, [&](const InterfaceFileRef &ref2) {
          return ref1.getInstallName() == ref2.getInstallName();
        });

        if (it == rhs.end()) {
          diag.report(diagID_missing) << "binary file" << ref1;
          return false;
        }

        if (it->getArchitectures() != ref1.getArchitectures()) {
          diag.report(diagID_mismatch) << ref1 << *it;
          return false;
        }
      }
      for (const auto &ref2 : rhs) {
        auto it = find_if(lhs, [&](const InterfaceFileRef &ref1) {
          return ref1.getInstallName() == ref2.getInstallName();
        });

        if (it == lhs.end()) {
          diag.report(diagID_missing) << "tapi option" << ref2;
          return false;
        }

        if (it->getArchitectures() != ref2.getArchitectures()) {
          llvm_unreachable("this case was already covered above.");
        }
      }
      llvm_unreachable("should have found a difference by now.");
    }
    return true;
  };

  if (!compareInterfaceFileRef(apiFile->reexportedLibraries(),
                               dylibFile->reexportedLibraries(),
                               diag::err_reexported_libraries_missing,
                               diag::err_reexported_libraries_mismatch))
    return false;

  if (!compareInterfaceFileRef(apiFile->allowableClients(),
                               dylibFile->allowableClients(),
                               diag::err_allowable_clients_missing,
                               diag::err_allowable_clients_mismatch))
    return false;

  auto compareUmbrellas =
      [&](const std::vector<std::pair<Target, std::string>> &lhs,
          const std::vector<std::pair<Target, std::string>> &rhs,
          unsigned diagID_mismatch) {
        std::string lhsUmbrellaName, rhsUmbrellaName;
        for (const auto &it : lhs) {
          lhsUmbrellaName = it.second;
          break;
        }
        for (const auto &it : rhs) {
          rhsUmbrellaName = it.second;
          break;
        }

        if (lhsUmbrellaName != rhsUmbrellaName) {
          diag.report(diagID_mismatch) << lhsUmbrellaName << rhsUmbrellaName;
          return false;
        }
        return true;
      };

  compareUmbrellas(apiFile->umbrellas(), dylibFile->umbrellas(),
                   diag::warn_parent_umbrella_mismatch);

  if (dylibFile->isTwoLevelNamespace() == false) {
    diag.report(diag::err_no_twolevel_namespace);
    return false;
  }

  return verifySymbols(apiFile, dylibFile, diag, verificationMode, demangle);
}

static Expected<std::unique_ptr<InterfaceFile>>
getCodeCoverageSymbols(DiagnosticsEngine &diag,
                       const std::vector<Triple> &targets,
                       const std::string &isysroot) {
  static int staticSymbol;
  // Try to find clang first in the toolchain. If that fails, then fall-back to
  // the default search PATH.
  auto mainExecutable = sys::fs::getMainExecutable("tapi", &staticSymbol);
  StringRef toolchainBinDir = sys::path::parent_path(mainExecutable);
  auto clangBinary =
      sys::findProgramByName("clang", makeArrayRef(toolchainBinDir));
  if (clangBinary.getError()) {
    diag.report(diag::warn) << "cannot find 'clang' in toolchain directory. "
                               "Looking for 'clang' in PATH instead.";
    clangBinary = sys::findProgramByName("clang");
    if (auto ec = clangBinary.getError())
      return make_error<StringError>("unable to find 'clang' in PATH", ec);
  }

  // Create temporary input and output files.
  SmallString<PATH_MAX> inputFile;
  if (auto ec = sys::fs::createTemporaryFile("code_coverage", "c", inputFile))
    return make_error<StringError>("unable to create temporary input file", ec);
  FileRemover removeInputFile(inputFile);

  SmallString<PATH_MAX> outputFile;
  if (auto ec =
          sys::fs::createTemporaryFile("libcodecoverage", "dylib", outputFile))
    return make_error<StringError>("unable to create temporary output file",
                                   ec);
  FileRemover removeOutputFile(outputFile);

  std::error_code ec;
  raw_fd_ostream input(inputFile, ec, sys::fs::F_None);
  if (ec)
    return make_error<StringError>("cannot open input file", ec);
  input << "static int foo() { return 0; }\n";
  input.close();

  Registry registry;
  registry.addBinaryReaders();

  std::string installDir = toolchainBinDir;
  std::vector<std::unique_ptr<InterfaceFile>> files;
  for (const auto &target : targets) {
    const StringRef clangArgs[] = {clangBinary.get(),
                                   "-target",
                                   target.str(),
                                   "-dynamiclib",
                                   "-fprofile-instr-generate",
                                   "-fcoverage-mapping",
                                   "-isysroot",
                                   isysroot,
                                   "-o",
                                   outputFile,
                                   inputFile,
                                   "-v"};

    SmallString<PATH_MAX> stderrFile;
    if (auto ec = sys::fs::createTemporaryFile("stderr", "txt", stderrFile))
      return make_error<StringError>("unable to create temporary stderr file",
                                     ec);
    FileRemover removeStderrFile(stderrFile);

    const Optional<StringRef> redirects[] = {/*STDIN=*/llvm::None,
                                             /*STDOUT=*/llvm::None,
                                             /*STDERR=*/StringRef(stderrFile)};

    bool failed = sys::ExecuteAndWait(clangBinary.get(), clangArgs,
                                      /*env=*/llvm::None, redirects);

    if (failed) {
      auto bufferOr = MemoryBuffer::getFile(stderrFile);
      if (auto ec = bufferOr.getError())
        return make_error<StringError>("unable to read file", ec);

      std::string message = "'clang' invocation failed:\n";
      for (auto arg : clangArgs) {
        if (arg.empty())
          continue;
        message.append(arg).append(1, ' ');
      }
      message.append(1, '\n');
      message.append(bufferOr.get()->getBuffer());

      return make_error<StringError>(
          message, std::make_error_code(std::errc::not_supported));
    }

    auto bufferOr = MemoryBuffer::getFile(outputFile.str());
    if (auto ec = bufferOr.getError())
      return make_error<StringError>("unable to read file", ec);
    auto file =
        registry.readFile(std::move(bufferOr.get()), ReadFlags::Symbols);
    if (!file)
      return file.takeError();
    files.emplace_back(std::move(file.get()));
  }

  // Merge all the interface files into one.
  std::unique_ptr<InterfaceFile> output;
  for (auto &file : files) {
    if (!output) {
      output = std::move(file);
      continue;
    }

    auto result = output->merge(file.get());
    if (!result)
      return result.takeError();
    output = std::move(result.get());
  }

  return std::move(output);
}

struct SymbolAlias {
  std::string symbol;
  std::string alias;
};

static Expected<std::vector<SymbolAlias>> parseAliasList(FileManager &fm,
                                                         StringRef path) {
  auto file = fm.getFile(path);
  if (!file)
    return errorCodeToError(
        std::make_error_code(std::errc::no_such_file_or_directory));

  auto bufferOrErr = fm.getBufferForFile(file);
  if (!bufferOrErr)
    return errorCodeToError(bufferOrErr.getError());

  auto buffer = bufferOrErr.get()->getBuffer();
  SmallVector<StringRef, 16> lines;
  std::vector<SymbolAlias> aliases;
  buffer.split(lines, "\n", /*MaxSplit=*/-1, /*KeepEmpty=*/false);
  for (const auto &line : lines) {
    auto l = line.trim();
    if (l.empty())
      continue;

    // Skip comments
    if (l.startswith("#"))
      continue;

    StringRef symbol, alias;
    std::tie(symbol, alias) = l.split(' ');
    if (alias.empty())
      return make_error<StringError>("invalid alias list",
                                     inconvertibleErrorCode());

    aliases.emplace_back(SymbolAlias{symbol.str(), alias.str()});
  }

  return aliases;
}

static std::tuple<StringRef, XPIKind> parseSymbol(StringRef symbolName) {
  StringRef name;
  XPIKind kind;
  if (symbolName.startswith(".objc_class_name_")) {
    name = symbolName.drop_front(17);
    kind = XPIKind::ObjectiveCClass;
  } else if (symbolName.startswith("_OBJC_CLASS_$_")) {
    name = symbolName.drop_front(14);
    kind = XPIKind::ObjectiveCClass;
  } else if (symbolName.startswith("_OBJC_METACLASS_$_")) {
    name = symbolName.drop_front(18);
    kind = XPIKind::ObjectiveCClass;
  } else if (symbolName.startswith("_OBJC_EHTYPE_$_")) {
    name = symbolName.drop_front(15);
    kind = XPIKind::ObjectiveCClassEHType;
  } else if (symbolName.startswith("_OBJC_IVAR_$_")) {
    name = symbolName.drop_front(13);
    kind = XPIKind::ObjectiveCInstanceVariable;
  } else {
    name = symbolName;
    kind = XPIKind::GlobalSymbol;
  }
  return std::make_tuple(name, kind);
}

static bool handleAutoZipperList(DiagnosticsEngine &diag, Options &opts,
                                 InterfaceFile &interface) {
  // parse /AppleInternal/LinkerAutoZipperList.txt
  SmallString<PATH_MAX> linkListPath(opts.frontendOptions.isysroot);
  sys::path::append(linkListPath, "AppleInternal", "LinkerAutoZipperList.txt");
  // If fail to open the file, just silently return.
  auto file = opts.getFileManager().getFile(linkListPath);
  if (!file)
    return false;

  auto bufferOrErr = opts.getFileManager().getBufferForFile(file);
  if (!bufferOrErr)
    return false;

  auto buffer = bufferOrErr.get()->getBuffer();
  SmallVector<StringRef, 16> lines;
  buffer.split(lines, "\n", /*MaxSplit=*/-1, /*KeepEmpty=*/false);
  for (const auto &line : lines) {
    auto l = line.trim();
    if (l.empty())
      continue;
    // Skip comments
    if (l.startswith("#"))
      continue;
    // If found matching installName, add target variant.
    if (l == opts.linkerOptions.installName) {
      for (const auto &target :
           interface.targets(ArchitectureSet::All().clear(AK_i386)))
        interface.addTarget({target.architecture, Platform::macCatalyst});
      diag.report(diag::warn_auto_zippered);
      return true;
    }
  }
  return false;
}

namespace {
class FileListVisitor final : public FileListReader::Visitor {
  FileManager &fm;
  DiagnosticsEngine &diag;
  HeaderSeq &headerFiles;

public:
  FileListVisitor(FileManager &fm, DiagnosticsEngine &diag,
                  HeaderSeq &headerFiles)
      : fm(fm), diag(diag), headerFiles(headerFiles) {}

  void visitHeaderFile(HeaderType type, StringRef path) override {
    if (!fm.exists(path)) {
      diag.report(diag::err_no_such_header_file) << path << (unsigned)type;
      return;
    }
    headerFiles.emplace_back(path, type);
  }
};
} // end anonymous namespace.

/// \brief Parses the headers and generate a text-based stub file.
bool Driver::InstallAPI::run(DiagnosticsEngine &diag, Options &opts) {
  auto &fm = opts.getFileManager();

  // Handle targets.
  if (opts.frontendOptions.targets.empty()) {
    diag.report(diag::err_no_target);
    return false;
  }

  // Set default language option.
  if (opts.frontendOptions.language == clang::InputKind::Unknown)
    opts.frontendOptions.language = clang::InputKind::ObjC;

  // Handle install name.
  if (opts.linkerOptions.installName.empty()) {
    diag.report(diag::err_no_install_name);
    return false;
  }

  SmallString<PATH_MAX> name =
      sys::path::filename(opts.linkerOptions.installName);
  sys::path::replace_extension(name, "");
  globalSnapshot->setName(name);

  // Handle platform.
  if (mapToPlatformSet(opts.frontendOptions.targets).count(Platform::unknown)) {
    diag.report(diag::err_no_deployment_target);
    return false;
  }

  diag.setErrorLimit(opts.diagnosticsOptions.errorLimit);

  // Lookup re-exported libraries.
  InterfaceFileManager manager(fm);
  PathSeq frameworkSearchPaths;
  std::vector<std::pair<std::string, ArchitectureSet>> reexportedLibraries;
  std::vector<const InterfaceFile *> reexportedLibraryFiles;
  for (auto &path : opts.frontendOptions.systemFrameworkPaths)
    frameworkSearchPaths.emplace_back(path);
  for (auto &path : opts.frontendOptions.frameworkPaths)
    frameworkSearchPaths.emplace_back(path);

  for (auto &it : opts.linkerOptions.reexportedLibraries) {
    auto name = "lib" + it.first + ".dylib";
    auto path =
        findLibrary(name, fm, {}, opts.frontendOptions.libraryPaths, {});
    if (path.empty()) {
      diag.report(diag::err_cannot_find) << "re-exported library" << it.first;
      return false;
    }

    auto file = manager.readFile(path);
    if (!file) {
      diag.report(diag::err_cannot_read_file)
          << path << toString(file.takeError());
      return false;
    }

    reexportedLibraries.emplace_back(file.get()->getInstallName(), it.second);
    reexportedLibraryFiles.emplace_back(file.get());
  }

  for (auto &it : opts.linkerOptions.reexportedLibraryPaths) {
    auto file = manager.readFile(it.first);
    if (!file) {
      diag.report(diag::err_cannot_read_file)
          << it.first << toString(file.takeError());
      return false;
    }

    reexportedLibraries.emplace_back(file.get()->getInstallName(), it.second);
    reexportedLibraryFiles.emplace_back(file.get());
  }

  for (auto &it : opts.linkerOptions.reexportedFrameworks) {
    auto name = it.first + ".framework/" + it.first;
    auto path = findLibrary(name, fm, frameworkSearchPaths, {}, {});
    if (path.empty()) {
      diag.report(diag::err_cannot_find) << "re-exported framework" << it.first;
      return false;
    }

    auto file = manager.readFile(path);
    if (!file) {
      diag.report(diag::err_cannot_read_file)
          << path << toString(file.takeError());
      return false;
    }

    reexportedLibraries.emplace_back(file.get()->getInstallName(), it.second);
    reexportedLibraryFiles.emplace_back(file.get());
  }

  if (opts.driverOptions.inputs.empty() && opts.tapiOptions.fileList.empty()) {
    diag.report(clang::diag::err_drv_no_input_files);
    return false;
  }

  PathSeq inputPaths;
  for (const auto &path : opts.driverOptions.inputs) {
    if (sys::path::extension(path) == ".json") {
      opts.tapiOptions.fileList = path;
      continue;
    }
    inputPaths.emplace_back(path);
  }

  FrontendJob job;
  job.workingDirectory = globalSnapshot->getWorkingDirectory();
  job.cacheFactory = newFileSystemStatCacheFactory<StatRecorder>();
  job.vfs = fm.getVirtualFileSystem();
  job.language = opts.frontendOptions.language;
  job.language_std = opts.frontendOptions.language_std;
  job.useRTTI = opts.frontendOptions.useRTTI;
  job.visibility = opts.frontendOptions.visibility;
  job.isysroot = opts.frontendOptions.isysroot;
  job.macros = opts.frontendOptions.macros;
  job.systemFrameworkPaths = opts.frontendOptions.systemFrameworkPaths;
  job.systemIncludePaths = opts.frontendOptions.systemIncludePaths;
  job.frameworkPaths = opts.frontendOptions.frameworkPaths;
  job.includePaths = opts.frontendOptions.includePaths;
  job.clangExtraArgs = opts.frontendOptions.clangExtraArgs;
  job.enableModules = opts.frontendOptions.enableModules;
  job.moduleCachePath = opts.frontendOptions.moduleCachePath;
  job.validateSystemHeaders = opts.frontendOptions.validateSystemHeaders;
  job.clangResourcePath = opts.frontendOptions.clangResourcePath;
  job.useObjectiveCARC = opts.frontendOptions.useObjectiveCARC;
  job.useObjectiveCWeakARC = opts.frontendOptions.useObjectiveCWeakARC;
  job.verbose = opts.frontendOptions.verbose;
  job.clangExecutablePath = opts.driverOptions.clangExecutablePath;

  //
  // Scan through the directories and create a list of all found frameworks.
  //
  HeaderSeq headerFiles;
  std::string frameworkName;

  if (!inputPaths.empty()) {
    DirectoryScanner scanner(fm, diag,
                             opts.linkerOptions.isDynamicLibrary
                                 ? ScannerMode::ScanDylibs
                                 : ScannerMode::ScanFrameworks);

    for (const auto &path : inputPaths) {
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

    // Only infer framework path when modules are enabled.
    if (opts.frontendOptions.enableModules) {
      job.frameworkPaths.insert(job.frameworkPaths.begin(),
                                sys::path::parent_path(framework->getPath()));
    }

    if (!framework->_versions.empty())
      framework = &framework->_versions.back();

    frameworkName = sys::path::stem(framework->getName());
    for (const auto &header : framework->_headerFiles) {
      auto *file = fm.getFile(header.fullPath);
      if (!file) {
        diag.report(diag::err_no_such_header_file)
            << header.fullPath << (unsigned)header.type;
        return false;
      }
      headerFiles.emplace_back(header);
    }

    // Only use system style includes when modules are enabled.
    if (opts.frontendOptions.enableModules) {
      if (!framework->isDynamicLibrary) {
        for (auto &header : headerFiles)
          header.includeName =
              "<" + frameworkName + '/' + header.relativePath + ">";
      }
    }
  }

  if (!opts.tapiOptions.fileList.empty()) {
    auto *file = fm.getFile(opts.tapiOptions.fileList);
    if (!file) {
      diag.report(clang::diag::err_drv_no_such_file)
          << opts.tapiOptions.fileList;
      return false;
    }
    auto bufferOr = fm.getBufferForFile(file);
    if (auto ec = bufferOr.getError()) {
      diag.report(diag::err_cannot_read_file)
          << file->getName() << ec.message();
      return false;
    }
    auto reader = FileListReader::get(std::move(bufferOr.get()));
    if (!reader) {
      diag.report(diag::err_cannot_read_file)
          << file->getName() << toString(reader.takeError());
      return false;
    }

    FileListVisitor visitor(fm, diag, headerFiles);
    reader.get()->visit(visitor);
    if (diag.hasErrorOccurred())
      return false;
  }

  for (const auto &path : opts.tapiOptions.extraPublicHeaders) {
    if (fm.exists(path)) {
      SmallString<PATH_MAX> fullPath(path);
      fm.makeAbsolutePath(fullPath);
      headerFiles.emplace_back(fullPath, HeaderType::Public);
      headerFiles.back().isExtra = true;
    } else {
      diag.report(diag::err_no_such_header_file)
          << path << (unsigned)HeaderType::Public;
      return false;
    }
  }

  for (const auto &path : opts.tapiOptions.extraPrivateHeaders) {
    if (fm.exists(path)) {
      SmallString<PATH_MAX> fullPath(path);
      fm.makeAbsolutePath(fullPath);
      headerFiles.emplace_back(fullPath, HeaderType::Private);
      headerFiles.back().isExtra = true;
    } else {
      diag.report(diag::err_no_such_header_file)
          << path << (unsigned)HeaderType::Private;
      return false;
    }
  }

  for (const auto &path : opts.tapiOptions.extraProjectHeaders) {
    if (fm.exists(path)) {
      SmallString<PATH_MAX> fullPath(path);
      fm.makeAbsolutePath(fullPath);
      headerFiles.emplace_back(fullPath, HeaderType::Project);
      headerFiles.back().isExtra = true;
    } else {
      diag.report(diag::err_no_such_header_file)
          << path << (unsigned)HeaderType::Project;
      return false;
    }
  }

  std::vector<std::unique_ptr<HeaderGlob>> excludeHeaderGlobs;
  std::set<const FileEntry *> excludeHeaderFiles;
  auto parseGlobs = [&](const PathSeq &paths, HeaderType type) {
    for (const auto &str : paths) {
      auto glob = HeaderGlob::create(str, type);
      if (glob)
        excludeHeaderGlobs.emplace_back(std::move(glob.get()));
      else {
        consumeError(glob.takeError());
        if (auto file = fm.getFile(str))
          excludeHeaderFiles.emplace(file);
        else {
          diag.report(diag::err_no_such_header_file) << str << (unsigned)type;
          return false;
        }
      }
    }
    return true;
  };

  if (!parseGlobs(opts.tapiOptions.excludePublicHeaders, HeaderType::Public))
    return false;

  if (!parseGlobs(opts.tapiOptions.excludePrivateHeaders, HeaderType::Private))
    return false;

  if (!parseGlobs(opts.tapiOptions.excludeProjectHeaders, HeaderType::Project))
    return false;

  for (auto &header : headerFiles) {
    for (auto &glob : excludeHeaderGlobs)
      if (glob->match(header))
        header.isExcluded = true;
  }

  if (!excludeHeaderFiles.empty()) {
    for (auto &header : headerFiles) {
      const auto *file = fm.getFile(header.fullPath);
      if (excludeHeaderFiles.count(file))
        header.isExcluded = true;
    }
  }

  for (const auto &glob : excludeHeaderGlobs)
    if (!glob->didMatch())
      diag.report(diag::warn_glob_did_not_match) << glob->str();

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

  const auto &publicUmbrellaHeaderPath =
      opts.tapiOptions.publicUmbrellaHeaderPath;
  if (!publicUmbrellaHeaderPath.empty()) {
    auto escapedString = Regex::escape(publicUmbrellaHeaderPath);
    Regex umbrellaRegex(escapedString);

    if (!matchAndMarkUmbrella(headerFiles, umbrellaRegex, HeaderType::Public)) {
      diag.report(diag::err_no_such_umbrella_header_file)
          << publicUmbrellaHeaderPath << (unsigned)HeaderType::Public;
      return false;
    }
  } else if (!frameworkName.empty()) {
    auto umbrellaName = "/" + Regex::escape(frameworkName) + "\\.h";
    Regex umbrellaRegex(umbrellaName);

    matchAndMarkUmbrella(headerFiles, umbrellaRegex, HeaderType::Public);
  }

  const auto &privateUmbrellaHeaderPath =
      opts.tapiOptions.privateUmbrellaHeaderPath;
  if (!privateUmbrellaHeaderPath.empty()) {
    auto escapedString = Regex::escape(privateUmbrellaHeaderPath);
    Regex umbrellaRegex(escapedString);

    if (!matchAndMarkUmbrella(headerFiles, umbrellaRegex,
                              HeaderType::Private)) {
      diag.report(diag::err_no_such_umbrella_header_file)
          << privateUmbrellaHeaderPath << (unsigned)HeaderType::Private;
      return false;
    }
  } else if (!frameworkName.empty()) {
    auto umbrellaName = "/" + Regex::escape(frameworkName) + "[_]?Private\\.h";
    Regex umbrellaRegex(umbrellaName);

    matchAndMarkUmbrella(headerFiles, umbrellaRegex, HeaderType::Private);
  }

  // Infer additional include paths.
  std::set<std::string> inferredIncludePaths;
  if (opts.tapiOptions.inferIncludePaths) {
    for (const auto &header : headerFiles) {
      // Never infer include pathd for project headers.
      if (header.type == HeaderType::Project)
        continue;
      if (header.isExcluded)
        continue;
      inferredIncludePaths.insert(sys::path::parent_path(header.fullPath));

      auto n = header.fullPath.rfind("/include/");
      if (n == std::string::npos)
        continue;
      auto path = header.fullPath.substr(0, n + 8);
      inferredIncludePaths.insert(path);
    }
  }

  job.includePaths.insert(job.includePaths.begin(),
                          inferredIncludePaths.begin(),
                          inferredIncludePaths.end());

  // Only sort the headers for framework that didn't have a json input file.
  // Fixme: Need to fix all projects that still depend on this behavior.
  if (!inputPaths.empty())
    std::stable_sort(headerFiles.begin(), headerFiles.end());
  job.headerFiles = headerFiles;

  std::vector<Triple> allTargets;
  allTargets.insert(allTargets.end(), opts.frontendOptions.targets.begin(),
                    opts.frontendOptions.targets.end());
  allTargets.insert(allTargets.end(),
                    opts.frontendOptions.targetVariants.begin(),
                    opts.frontendOptions.targetVariants.end());

  std::vector<FrontendContext> frontendResults;
  for (auto &target : allTargets) {
    job.target = target;
    for (auto type :
         {HeaderType::Public, HeaderType::Private, HeaderType::Project}) {
      job.type = type;
      auto result = runFrontend(job);
      if (!result)
        return false;
      frontendResults.emplace_back(std::move(result.getValue()));
    }
  }

  if (opts.tapiOptions.printAfter == "frontend") {
    APIPrinter printer(errs());
    for (auto &result : frontendResults) {
      errs() << "triple:" << result.target.str() << "\n";
      result.visit(printer);
      errs() << "\n";
    }
  }


  auto headerSymbols = make_unique<XPISet>();
  for (auto &result : frontendResults) {
    API2XPIConverter converter(headerSymbols.get(), result.target);
    result.visit(converter);
  }

  auto scanFile = make_unique<InterfaceFile>(std::move(headerSymbols));
  scanFile->addTargets(allTargets);
  scanFile->setInstallName(opts.linkerOptions.installName);
  scanFile->setCurrentVersion(opts.linkerOptions.currentVersion);
  scanFile->setCompatibilityVersion(opts.linkerOptions.compatibilityVersion);
  scanFile->setTwoLevelNamespace();
  scanFile->setApplicationExtensionSafe(
      opts.linkerOptions.isApplicationExtensionSafe);
  scanFile->setInstallAPI();
  for (const auto &lib : opts.linkerOptions.allowableClients)
    for (const auto &target : scanFile->targets(lib.architectures))
      scanFile->addAllowableClient(lib.installName, target);
  for (const auto &lib : opts.linkerOptions.reexportInstallNames)
    for (const auto &target : scanFile->targets(lib.architectures))
      scanFile->addReexportedLibrary(lib.installName, target);
  for (const auto &lib : reexportedLibraries)
    for (const auto &target : scanFile->targets(lib.second))
      scanFile->addReexportedLibrary(lib.first, target);
  if (!opts.frontendOptions.umbrella.empty()) {
    for (const auto &target : scanFile->targets())
      scanFile->addParentUmbrella(target, opts.frontendOptions.umbrella);
  }
  if (opts.tapiOptions.printAfter == "xpi")
    scanFile->printSymbols();

  // Add symbols from alias lists.
  for (const auto &it : opts.linkerOptions.aliasLists) {
    auto result = parseAliasList(fm, it.first);
    if (!result) {
      diag.report(diag::err)
          << "could not read alias list" << toString(result.takeError());
      return false;
    }

    for (const auto &sym : result.get()) {
      auto symbol = parseSymbol(sym.symbol);
      auto alias = parseSymbol(sym.alias);

      // Check if the base symbol exists.
      APIAccess access = APIAccess::Private;
      if (auto xpiSymbol =
              scanFile->contains(std::get<1>(symbol), std::get<0>(symbol)))
        access = (*xpiSymbol)->getAccess();

      scanFile->addSymbol(std::get<1>(alias), std::get<0>(alias),
                          scanFile->targets(it.second), APILinkage::Exported,
                          APIFlags::None, access);
    }
  }
  if (opts.tapiOptions.printAfter == "alias_list")
    scanFile->printSymbols();

  // Remove symbols that come from re-exported frameworks.
  for (const auto &file : reexportedLibraryFiles)
    for (const auto &sym : file->exports())
      scanFile->removeSymbol(sym->getKind(), sym->getName());

  if (opts.tapiOptions.printAfter == "reexport_framework")
    scanFile->printSymbols();

  // Check to see if we need to AutoZipper the output.
  // If auto zippered, add ios mac to the platform.
  bool autoZippered = false;
  if (scanFile->getPlatforms().count(Platform::macOS) &&
      !scanFile->getPlatforms().count(Platform::macCatalyst))
    autoZippered = handleAutoZipperList(diag, opts, *scanFile);

  // When code coverage is enabled we need to generate extra symbols manually.
  // These symbols are defined in libclang_rt.profile_*.a and are pulled in by
  // clang when -fprofile-instr-generate is specified on the command line.
  //
  // This needs to happen after we removed the re-exported library symbols, or
  // we will remove the code coverage symbols too.
  if (opts.tapiOptions.generateCodeCoverageSymbols) {
    auto file =
        getCodeCoverageSymbols(diag, allTargets, opts.frontendOptions.isysroot);
    if (!file) {
      diag.report(diag::err) << "could not generate coverage symbols"
                             << toString(file.takeError());
      return false;
    }
    for (const auto *symbol : file.get()->exports()) {
      scanFile->addSymbol(symbol->getKind(), symbol->getName(),
                          symbol->targets(), symbol->getLinkage(),
                          symbol->getFlags(), symbol->getAccess());
    }
  }
  if (opts.tapiOptions.printAfter == "code_coverage")
    scanFile->printSymbols();

  if (!opts.tapiOptions.verifyAgainst.empty()) {
    auto dylib = manager.readFile(opts.tapiOptions.verifyAgainst);
    if (!dylib) {
      diag.report(diag::err_cannot_read_file)
          << opts.tapiOptions.verifyAgainst << toString(dylib.takeError());
      return false;
    }

    if (!verifyFramework(scanFile.get(), dylib.get(), diag,
                         opts.tapiOptions.verificationMode,
                         opts.tapiOptions.demangle, autoZippered))
      return false;

    // Clear the installapi flag.
    scanFile->setInstallAPI(false);

    // Record the UUIDs from the dynamic library.
    if (opts.tapiOptions.recordUUIDs)
      for (auto &uuid : dylib.get()->uuids())
        scanFile->addUUID(uuid.first, uuid.second);
  }

  if (opts.driverOptions.outputPath.empty()) {
    SmallString<PATH_MAX> path;
    if (auto ec = sys::fs::current_path(path)) {
      diag.report(diag::err) << path << ec.message();
      return false;
    }
    auto targetName = sys::path::stem(opts.linkerOptions.installName);
    sys::path::append(path, targetName);
    TAPI_INTERNAL::replace_extension(path, ".tbd");
    opts.driverOptions.outputPath = path.str();
  }

  SmallString<PATH_MAX> outputDir(opts.driverOptions.outputPath);
  sys::path::remove_filename(outputDir);
  auto ec = sys::fs::create_directories(outputDir);
  if (ec) {
    diag.report(diag::err_cannot_create_directory) << outputDir << ec.message();
    return false;
  }

  auto result = manager.writeFile(opts.driverOptions.outputPath, scanFile.get(),
                                  opts.tapiOptions.fileType);
  if (result) {
    diag.report(diag::err_cannot_write_file)
        << opts.driverOptions.outputPath << toString(std::move(result));
    return false;
  }
  globalSnapshot->recordFile(opts.driverOptions.outputPath);

  return true;
}

TAPI_NAMESPACE_INTERNAL_END
