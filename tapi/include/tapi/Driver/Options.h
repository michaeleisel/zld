//===--- tapi/Driver/Options.h - Options ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef TAPI_DRIVER_OPTIONS_H
#define TAPI_DRIVER_OPTIONS_H

#include "tapi/Core/FileManager.h"
#include "tapi/Core/InterfaceFile.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Core/PackedVersion.h"
#include "tapi/Core/Path.h"
#include "tapi/Core/Platform.h"
#include "tapi/Defines.h"
#include "tapi/Diagnostics/Diagnostics.h"
#include "tapi/Driver/DriverOptions.h"
#include "clang/Frontend/FrontendOptions.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Option/Option.h"
#include <set>
#include <string>
#include <vector>

TAPI_NAMESPACE_INTERNAL_BEGIN

class Snapshot;

using Macro = std::pair<std::string, bool /*isUndef*/>;

/// \brief A list of supported TAPI commands.
enum class TAPICommand : unsigned {
  Driver,
  Archive,
  Stubify,
  InstallAPI,
  Reexport,
  GenerateAPITests,
};

/// \brief A list of InstallAPI verification modes.
enum class VerificationMode {
  Invalid,
  ErrorsOnly,
  ErrorsAndWarnings,
  Pedantic,
};

/// \brief Archive action.
enum class ArchiveAction {
  Unknown,

  /// \brief Print the architectures in the input file.
  ShowInfo,

  /// \brief Specify the architecture to extract from the input file.
  ExtractArchitecture,

  /// \brief Specify the architecture to remove from the input file.
  RemoveArchitecture,

  /// \brief Verify the architecture exists in the input file.
  VerifyArchitecture,

  /// \brief Merge the input files.
  Merge,

  /// \brief List the exported symbols.
  ListSymbols,
};

/// \brief Snapshot mode.
enum class SnapshotMode {
  /// \brief Record all options and accessed files. Only creates the snapshot in
  ///        case of an error.
  Create,

  /// \brief Always create a snapshot and record all options and accessed files.
  ForceCreate,

  /// \brief Load an existing snapshot and reply it.
  Load,
};

struct LibraryRef {
  std::string installName;
  ArchitectureSet architectures;

  LibraryRef() = default;

  LibraryRef(const std::string &name, ArchitectureSet architectures)
      : installName(name), architectures(architectures) {}
};

static inline bool operator==(const LibraryRef &lhs, const LibraryRef &rhs) {
  return std::tie(lhs.installName, lhs.architectures) ==
         std::tie(rhs.installName, rhs.architectures);
}

struct SnapshotOptions {
  /// \brief Snapshot mode.
  SnapshotMode snapshotMode = SnapshotMode::Create;

  /// \brief Snapshot output directory.
  std::string snapshotOutputDir;

  /// \brief Snapshot input path. This can be a snapshot directory or a
  ///        runscript inside a snapshot directory).
  std::string snapshotInputPath;

  /// \brief Use own ressource directory. Override the content of the ressource
  ///        directory provided by the snapshot with our own files.
  bool useOwnResourceDir = false;
};

struct DriverOptions {
  /// \brief Print version informtion.
  bool printVersion = false;

  /// \brief Print help.
  bool printHelp = false;

  /// \brief Print hidden options too.
  bool printHelpHidden = false;

  /// \brief List of input paths.
  PathSeq inputs;

  /// \brief Output path.
  std::string outputPath;

  /// VFS Overlay paths.
  PathSeq vfsOverlayPaths;

  /// Clang executable path.
  std::string clangExecutablePath;
};

struct ArchiveOptions {
  /// \brief Specifies which archive action to run.
  ArchiveAction action = ArchiveAction::Unknown;

  /// \brief Specifies the archive action architecture to use (if applicable).
  Architecture arch = AK_unknown;

  /// \brief This allows merging of TBD files containing the same architecture.
  bool allowArchitectureMerges = false;
};

struct LinkerOptions {
  /// \brief The install name to use for the dynamic library.
  std::string installName;

  /// \brief The current version to use for the dynamic library.
  PackedVersion currentVersion;

  /// \brief The compatibility version to use for the dynamic library.
  PackedVersion compatibilityVersion;

  /// \brief Set if we should scan for a dynamic library and not a framework.
  bool isDynamicLibrary = false;

  /// \brief List of allowable clients to use for the dynamic library.
  std::vector<LibraryRef> allowableClients;

  /// \brief List of reexported libraries to use for the dynamic library.
  std::vector<LibraryRef> reexportInstallNames;

  /// \brief List of reexported libraries to use for the dynamic library.
  std::vector<std::pair<std::string, ArchitectureSet>> reexportedLibraries;

  /// \brief List of reexported libraries to use for the dynamic library.
  std::vector<std::pair<std::string, ArchitectureSet>> reexportedLibraryPaths;

  /// \brief List of reexported frameworks to use for the dynamic library.
  std::vector<std::pair<std::string, ArchitectureSet>> reexportedFrameworks;

  /// \brief Is application extension safe.
  bool isApplicationExtensionSafe = false;

  /// \brief Path to the alias list file.
  std::vector<std::pair<std::string, ArchitectureSet>> aliasLists;
};

struct FrontendOptions {
  /// \brief Targets to build for.
  std::vector<llvm::Triple> targets;

  /// \brief Additonal target variants to build for.
  std::vector<llvm::Triple> targetVariants;

  /// \brief Specify the language to use for parsing.
  clang::InputKind::Language language = clang::InputKind::Unknown;

  /// \brief Language standard to use for parsing.
  std::string language_std;

  /// \brief The sysroot to search for SDK headers.
  std::string isysroot;

  /// \brief Name of the umbrella framework.
  std::string umbrella;

  /// \brief Additional SYSTEM framework search paths.
  PathSeq systemFrameworkPaths;

  /// \brief Additional framework search paths.
  PathSeq frameworkPaths;

  /// \brief Additional library search paths.
  PathSeq libraryPaths;

  /// \brief Additional SYSTEM include paths.
  PathSeq systemIncludePaths;

  /// \brief Additional include paths.
  PathSeq includePaths;

  /// \brief Macros to use for for parsing.
  std::vector<Macro> macros;

  /// \brief Use RTTI.
  bool useRTTI = true;

  /// \brief Set the visibility.
  /// TODO: We should disallow this for header parsing, but we could still use
  /// it for warnings.
  std::string visibility;

  /// \brief Use modules.
  bool enableModules = false;

  /// \brief Module cache path.
  std::string moduleCachePath;

  /// \brief Validate system headers when using modules.
  bool validateSystemHeaders = false;

  /// \brief Additional clang flags to be passed to the parser.
  std::vector<std::string> clangExtraArgs;

  /// \brief Clang resource path.
  std::string clangResourcePath;

  /// \brief Use Objective-C ARC (-fobjc-arc).
  bool useObjectiveCARC = false;

  /// \brief Use Objective-C weak ARC (-fobjc-weak).
  bool useObjectiveCWeakARC = false;

  /// \brief Verbose, show scan content and options.
  bool verbose = false;
};

struct DiagnosticsOptions {
  /// \brief Output path for the serialized diagnostics file.
  std::string serializeDiagnosticsFile;

  /// \brief Error limit.
  unsigned errorLimit = 0;
};

struct TAPIOptions {
  /// Path to file list (JSON).
  std::string fileList;

  /// \brief Path to public umbrella header.
  std::string publicUmbrellaHeaderPath;

  /// \brief Path to private umbrella header.
  std::string privateUmbrellaHeaderPath;

  /// \brief List of extra public header files.
  PathSeq extraPublicHeaders;

  /// \brief List of extra private header files.
  PathSeq extraPrivateHeaders;

  /// List of extra project header files.
  PathSeq extraProjectHeaders;

  /// \brief List of excluded public header files.
  PathSeq excludePublicHeaders;

  /// \brief List of excluded private header files.
  PathSeq excludePrivateHeaders;

  /// \brief List of excluded project header files.
  PathSeq excludeProjectHeaders;

  /// \brief Path to dynamic library for verification.
  std::string verifyAgainst;

  /// \brief Verification mode.
  VerificationMode verificationMode = VerificationMode::ErrorsOnly;

  /// \brief Generate additional symbols for code coverage.
  bool generateCodeCoverageSymbols = false;

  /// \brief Demangle symbols (C++) when printing.
  bool demangle = false;

  /// \brief Delete input file after stubbing.
  bool deleteInputFile = false;

  /// \brief Inline private frameworks.
  bool inlinePrivateFrameworks = false;

  /// \brief Delete private frameworks.
  bool deletePrivateFrameworks = false;

  /// \brief Record UUIDs.
  bool recordUUIDs = true;

  /// \brief Set 'installapi' flag.
  bool setInstallAPIFlag = false;


  /// \brief Specify the output file type.
  VersionedFileType fileType = TBDv3;


  /// \brief Infer the include paths based on the provided/found header files.
  bool inferIncludePaths = true;

  /// \brief Print the API/XPI after a certain phase.
  std::string printAfter;

  /// \brief Verify the API of zippered frameworks.
  bool verifyAPI = true;

  /// \brief Skip external headers when verifying the API of a zippered
  /// framework.
  bool verifyAPISkipExternalHeaders = true;

  /// \brief Emit API verification errors as warning.
  bool verifyAPIErrorAsWarning = false;

  /// \brief Whitelist YAML file for API verification.
  std::string verifyAPIWhitelist; // EquivalentTypes.conf
};


class Options {
private:
  /// Helper methods for handling the various options.
  bool processSnapshotOptions(DiagnosticsEngine &diag,
                              llvm::opt::InputArgList &args);

  bool processXarchOptions(DiagnosticsEngine &diag,
                           llvm::opt::InputArgList &args);

  bool processDriverOptions(DiagnosticsEngine &diag,
                            llvm::opt::InputArgList &args);

  bool processArchiveOptions(DiagnosticsEngine &diag,
                             llvm::opt::InputArgList &args);

  bool processLinkerOptions(DiagnosticsEngine &diag,
                            llvm::opt::InputArgList &args);

  bool processFrontendOptions(DiagnosticsEngine &diag,
                              llvm::opt::InputArgList &args);

  bool processDiagnosticsOptions(DiagnosticsEngine &diag,
                                 llvm::opt::InputArgList &args);

  bool processTAPIOptions(DiagnosticsEngine &diag,
                          llvm::opt::InputArgList &args);


  void initOptionsFromSnapshot(const Snapshot &snapshot);

public:
  /// \brief The TAPI command to run.
  TAPICommand command = TAPICommand::Driver;

  /// The various options grouped together.
  SnapshotOptions snapshotOptions;
  DriverOptions driverOptions;
  ArchiveOptions archiveOptions;
  LinkerOptions linkerOptions;
  FrontendOptions frontendOptions;
  DiagnosticsOptions diagnosticsOptions;
  TAPIOptions tapiOptions;

  Options() = delete;

  /// \brief Constructor for options.
  Options(DiagnosticsEngine &diag, ArrayRef<const char *> argString);

  FileManager &getFileManager() const { return *fm; }

  /// \brief Print the help depending on the recognized coomand.
  void printHelp() const;

private:
  std::string programName;
  std::unique_ptr<llvm::opt::OptTable> table;
  IntrusiveRefCntPtr<FileManager> fm;
  std::map<const llvm::opt::Arg *, Architecture> argToArchMap;

  friend class Snapshot;
  friend class Context;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_DRIVER_OPTIONS_H
