//===--- Snapshot.cpp - Snapshot ------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "tapi/Driver/Snapshot.h"
#include "tapi/Config/Version.h"
#include "tapi/Core/FileSystem.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Core/TextStubCommon.h"
#include "tapi/Defines.h"
#include "clang/Frontend/FrontendOptions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Config/config.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/xxhash.h"

using namespace llvm;
using namespace TAPI_INTERNAL;
using clang::InputKind;

using Mapping = std::pair<std::string, uint64_t>;
using Reexports = std::pair<std::string, ArchitectureSet>;
LLVM_YAML_IS_SEQUENCE_VECTOR(InterfaceFileRef)
LLVM_YAML_IS_SEQUENCE_VECTOR(Reexports)
LLVM_YAML_IS_SEQUENCE_VECTOR(LibraryRef)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(Macro)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(Triple)
LLVM_YAML_IS_STRING_MAP(uint64_t)

namespace llvm {
namespace yaml {

template <> struct ScalarEnumerationTraits<TAPICommand> {
  static void enumeration(IO &io, TAPICommand &command) {
    io.enumCase(command, "driver", TAPICommand::Driver);
    io.enumCase(command, "archive", TAPICommand::Archive);
    io.enumCase(command, "stubify", TAPICommand::Stubify);
    io.enumCase(command, "installapi", TAPICommand::InstallAPI);
    io.enumCase(command, "reexport", TAPICommand::Reexport);
  }
};

template <> struct ScalarEnumerationTraits<ArchiveAction> {
  static void enumeration(IO &io, ArchiveAction &action) {
    io.enumCase(action, "unknown", ArchiveAction::Unknown);
    io.enumCase(action, "show-info", ArchiveAction::ShowInfo);
    io.enumCase(action, "extract-architecture",
                ArchiveAction::ExtractArchitecture);
    io.enumCase(action, "remove-architecture",
                ArchiveAction::RemoveArchitecture);
    io.enumCase(action, "verify-architecture",
                ArchiveAction::VerifyArchitecture);
    io.enumCase(action, "merge", ArchiveAction::Merge);
  }
};

template <> struct MappingTraits<Reexports> {
  static void mapping(IO &io, Reexports &ref) {
    io.mapRequired("name", ref.first);
    io.mapRequired("architectures", ref.second);
  }
};

template <> struct MappingTraits<LibraryRef> {
  static void mapping(IO &io, LibraryRef &ref) {
    io.mapRequired("install-name", ref.installName);
    io.mapRequired("architectures", ref.architectures);
  }
};

template <> struct ScalarTraits<Triple> {
  static void output(const Triple &value, void * /*unused*/, raw_ostream &os) {
    os << value.str();
  }

  static StringRef input(StringRef scalar, void * /*unused*/, Triple &value) {
    value.setTriple(scalar);
    return {};
  }

  static QuotingType mustQuote(StringRef /*unused*/) {
    return QuotingType::None;
  }
};

template <> struct ScalarTraits<Macro> {
  static void output(const Macro &value, void * /*unused*/, raw_ostream &os) {
    if (value.second)
      os << "-U" << value.first;
    else
      os << "-D" << value.first;
  }

  static StringRef input(StringRef scalar, void * /*unused*/, Macro &value) {
    if (scalar.startswith("-D"))
      value = Macro(scalar.drop_front(2), /*isUndef=*/false);
    else if (scalar.startswith("-U"))
      value = Macro(scalar.drop_front(2), /*isUndef=*/true);
    else
      return "invalid macro";
    return {};
  }

  static QuotingType mustQuote(StringRef /*unused*/) {
    return QuotingType::Single;
  }
};

template <> struct ScalarEnumerationTraits<VerificationMode> {
  static void enumeration(IO &io, VerificationMode &mode) {
    io.enumCase(mode, "invalid", VerificationMode::Invalid);
    io.enumCase(mode, "errors-only", VerificationMode::ErrorsOnly);
    io.enumCase(mode, "errors-and-warnings",
                VerificationMode::ErrorsAndWarnings);
    io.enumCase(mode, "pedantic", VerificationMode::Pedantic);
  }
};

template <> struct MappingTraits<DriverOptions> {
  static void mapping(IO &io, DriverOptions &opts) {
    io.mapOptional("print-version", opts.printVersion, false);
    io.mapOptional("print-help", opts.printHelp, false);
    io.mapOptional("print-help-hidden", opts.printHelpHidden, false);
    io.mapOptional("inputs", opts.inputs, PathSeq{});
    io.mapOptional("output-path", opts.outputPath, std::string());
    io.mapOptional("vfs-overlay-paths", opts.vfsOverlayPaths, PathSeq{});
    io.mapOptional("clang-executable-path", opts.clangExecutablePath,
                   std::string{});
  }
};

template <> struct MappingTraits<ArchiveOptions> {
  static void mapping(IO &io, ArchiveOptions &opts) {
    io.mapOptional("action", opts.action, ArchiveAction::Unknown);
    io.mapOptional("architecture", opts.arch, AK_unknown);
  }
};

template <>
struct MappingContextTraits<LinkerOptions, Snapshot::MappingContext> {
  static void mapping(IO &io, LinkerOptions &opts,
                      Snapshot::MappingContext &ctx) {
    io.mapOptional("architectures", ctx.architectures, ArchitectureSet());
    io.mapOptional("install-name", opts.installName, std::string());
    io.mapOptional("current-version", opts.currentVersion, PackedVersion());
    io.mapOptional("compatibility-version", opts.compatibilityVersion,
                   PackedVersion());
    io.mapOptional("is-dynamic-library", opts.isDynamicLibrary, false);
    io.mapOptional("allowable-clients", opts.allowableClients,
                   std::vector<LibraryRef>{});
    io.mapOptional("reexported-libraries", opts.reexportInstallNames,
                   std::vector<LibraryRef>{});
    io.mapOptional("reexported-libraries2", opts.reexportedLibraries,
                   std::vector<std::pair<std::string, ArchitectureSet>>{});
    io.mapOptional("reexported-library-paths", opts.reexportedLibraryPaths,
                   std::vector<std::pair<std::string, ArchitectureSet>>{});
    io.mapOptional("reexported-frameworks", opts.reexportedFrameworks,
                   std::vector<std::pair<std::string, ArchitectureSet>>{});
    io.mapOptional("is-application-extension-safe",
                   opts.isApplicationExtensionSafe, false);
    io.mapOptional("alias-list", opts.aliasLists,
                   std::vector<std::pair<std::string, ArchitectureSet>>{});
  }
};

template <>
struct MappingContextTraits<tapi::internal::FrontendOptions,
                            Snapshot::MappingContext> {
  static void mapping(IO &io, tapi::internal::FrontendOptions &opts,
                      Snapshot::MappingContext &ctx) {
    io.mapOptional("platform", ctx.platform, Platform::unknown);
    io.mapOptional("os-version", ctx.osVersion, std::string());
    io.mapOptional("targets", opts.targets, std::vector<llvm::Triple>{});
    io.mapOptional("target-variants", opts.targetVariants,
                   std::vector<llvm::Triple>{});
    io.mapOptional("language", opts.language, InputKind::ObjC);
    io.mapOptional("language-std", opts.language_std, std::string());
    io.mapOptional("isysroot", opts.isysroot, std::string());
    io.mapOptional("umbrella", opts.umbrella, std::string());
    io.mapOptional("system-framework-paths", opts.systemFrameworkPaths,
                   PathSeq{});
    io.mapOptional("system-include-paths", opts.systemIncludePaths, PathSeq{});
    io.mapOptional("framework-paths", opts.frameworkPaths, PathSeq{});
    io.mapOptional("library-paths", opts.libraryPaths, PathSeq{});
    io.mapOptional("include-paths", opts.includePaths, PathSeq{});
    io.mapOptional("macros", opts.macros, std::vector<Macro>{});
    io.mapOptional("use-rtti", opts.useRTTI, true);
    io.mapOptional("visibility", opts.visibility, std::string());
    io.mapOptional("enable-modules", opts.enableModules, false);
    io.mapOptional("module-cache-path", opts.moduleCachePath, std::string());
    io.mapOptional("validate-system-headers", opts.validateSystemHeaders,
                   false);
    io.mapOptional("use-objc-arc", opts.useObjectiveCARC, false);
    io.mapOptional("use-objc-weak", opts.useObjectiveCWeakARC, false);
    io.mapOptional("clang-extra-args", opts.clangExtraArgs,
                   std::vector<std::string>{});
    io.mapOptional("clang-resource-path", opts.clangResourcePath,
                   std::string());
  }
};

template <> struct MappingTraits<DiagnosticsOptions> {
  static void mapping(IO &io, DiagnosticsOptions &opts) {
    io.mapOptional("serialize-diagnostics-file", opts.serializeDiagnosticsFile,
                   std::string());
    io.mapOptional("error-limit", opts.errorLimit, 0U);
  }
};

template <> struct MappingTraits<TAPIOptions> {
  static void mapping(IO &io, TAPIOptions &opts) {
    io.mapOptional("file-list", opts.fileList, std::string());
    io.mapOptional("public-umbrella-header-path", opts.publicUmbrellaHeaderPath,
                   std::string());
    io.mapOptional("private-umbrella-header-path",
                   opts.privateUmbrellaHeaderPath, std::string());
    io.mapOptional("extra-public-headers", opts.extraPublicHeaders, PathSeq{});
    io.mapOptional("extra-private-headers", opts.extraPrivateHeaders,
                   PathSeq{});
    io.mapOptional("extra-project-headers", opts.extraProjectHeaders,
                   PathSeq{});
    io.mapOptional("exclude-public-headers", opts.excludePublicHeaders,
                   PathSeq{});
    io.mapOptional("exclude-private-headers", opts.excludePrivateHeaders,
                   PathSeq{});
    io.mapOptional("exclude-project-headers", opts.excludeProjectHeaders,
                   PathSeq{});
    io.mapOptional("verify-against", opts.verifyAgainst, std::string());
    io.mapOptional("verification-mode", opts.verificationMode,
                   VerificationMode::ErrorsOnly);
    io.mapOptional("generate-code-coverage-symbols",
                   opts.generateCodeCoverageSymbols, false);
    io.mapOptional("demangle", opts.demangle, false);

    io.mapOptional("delete-input-file", opts.deleteInputFile, false);
    io.mapOptional("inline-private-frameworks", opts.inlinePrivateFrameworks,
                   false);
    io.mapOptional("delete-private-frameworks", opts.deletePrivateFrameworks,
                   false);
    io.mapOptional("record-uuids", opts.recordUUIDs, true);
    io.mapOptional("set-installapi-flag", opts.setInstallAPIFlag, false);
    io.mapOptional("infer-include-paths", opts.inferIncludePaths, true);
    io.mapOptional("verify-api", opts.verifyAPI, true);
    io.mapOptional("verify-api-skip-external-headers",
                   opts.verifyAPISkipExternalHeaders, false);
    io.mapOptional("verify-api-error-as-warning", opts.verifyAPIErrorAsWarning,
                   false);
  }
};

template <> struct CustomMappingTraits<FileMapping> {
  static void inputOne(IO &io, StringRef key, FileMapping &v) {
    io.mapRequired(key.str().c_str(), v[key]);
  }

  static void output(IO &io, FileMapping &v) {
    for (auto &p : v)
      io.mapRequired(p.first.c_str(), p.second);
  }
};

template <> struct MappingTraits<Snapshot> {
  static void mapping(IO &io, Snapshot &snapshot) {
    io.mapRequired("tapi-version", snapshot.tapiVersion);
    io.mapRequired("name", snapshot.name);
    io.mapRequired("command", snapshot.command);
    io.mapRequired("working-directory", snapshot.workingDirectory);
    io.mapOptional("raw-args", snapshot.rawArgs, std::vector<std::string>{});
    io.mapOptional("driver-options", snapshot.driverOptions);
    io.mapOptional("archive-options", snapshot.archiveOptions);
    io.mapOptionalWithContext("linker-options", snapshot.linkerOptions,
                              snapshot.context);
    io.mapOptionalWithContext("frontend-options", snapshot.frontendOptions,
                              snapshot.context);
    io.mapOptional("diagnostics-options", snapshot.diagnosticsOptions);
    io.mapOptional("tapi-options", snapshot.tapiOptions);
    io.mapOptional("directories", snapshot.normalizedDirectories);
    io.mapOptional("file-mapping", snapshot.pathToHash);
    io.mapOptional("symlink-mapping", snapshot.symlinkToPath);
  }
};

} // end namespace yaml.
} // end namespace llvm.

TAPI_NAMESPACE_INTERNAL_BEGIN

llvm::ManagedStatic<Snapshot> globalSnapshot;

static constexpr const char *filesDirectory = "Files";
static constexpr const char *runScriptsDirectory = "RunScript";

Snapshot::Snapshot() : tapiVersion(getTAPIFullVersion()) {}

Snapshot::~Snapshot() {
  if (!wantSnapshot)
    return;

  writeSnapshot(/*isCrash=*/false);
}

void Snapshot::findAndRecordSymlinks(SmallVectorImpl<char> &path,
                                     int level = 0) {
  if (level > 20) {
    outs() << path << ": Too many levels of symbolic links\n";
    return;
  }
  StringRef p(path.begin(), path.size());
  SmallString<PATH_MAX> currentPath;
  for (auto dir = sys::path::begin(p), e = sys::path::end(p); dir != e; ++dir) {
    sys::path::append(currentPath, *dir);
    if (directorySet.count(currentPath.str()))
      continue;

    auto fileType = sys::fs::get_file_type(currentPath, /*follow=*/false);
    switch (fileType) {
    case sys::fs::file_type::symlink_file: {
      SmallString<PATH_MAX> linkPath;
      if (auto ec = read_link(currentPath, linkPath)) {
        outs() << currentPath << ": " << ec.message() << "\n";
        return;
      }
      SmallString<PATH_MAX> newPath;
      if (sys::path::is_absolute(linkPath))
        newPath = linkPath;
      else {
        newPath = currentPath;
        sys::path::remove_filename(newPath);
        sys::path::append(newPath, linkPath);
      }

      if (auto ec = sys::fs::make_absolute(newPath)) {
        outs() << currentPath << ": " << ec.message() << "\n";
        return;
      }
      sys::path::remove_dots(newPath, /*remove_dot_dot=*/true);
      symlinkToPath[currentPath.str()] = newPath.str();

      for (auto itr = std::next(dir); itr != e; ++itr)
        sys::path::append(newPath, *itr);
      findAndRecordSymlinks(newPath, ++level);
      path = newPath;
      return;
    }
    case sys::fs::file_type::directory_file:
      directorySet.emplace(currentPath.str());
      break;
    default:
      return;
    }
  }
  return;
}

void Snapshot::writeSnapshot(bool isCrash) {
  if (snapshotWritten)
    return;
  SmallString<PATH_MAX> root(rootPath);
  if (auto ec = sys::fs::make_absolute(root)) {
    outs() << root << ": " << ec.message() << "\n";
    return;
  }
  sys::path::remove_dots(root, /*remove_dot_dot=*/true);

  auto perms = sys::fs::perms::owner_all | sys::fs::perms::group_read |
               sys::fs::perms::group_exe | sys::fs::perms::others_read |
               sys::fs::perms::others_exe;

  if (auto ec =
          sys::fs::create_directory(root, /*IgnoreExisting=*/true, perms)) {
    outs() << root << ": " << ec.message() << "\n";
    return;
  }

  sys::path::append(root, name);
  if (auto ec = sys::fs::createUniqueDirectory(root, root)) {
    outs() << root << ": " << ec.message() << "\n";
    return;
  }
  if (auto ec = sys::fs::setPermissions(root, perms)) {
    outs() << root << ": " << ec.message() << "\n";
    return;
  }

  for (auto *directory : {filesDirectory, runScriptsDirectory}) {
    SmallString<PATH_MAX> path = root;
    sys::path::append(path, directory);
    if (auto ec =
            sys::fs::create_directory(path, /*IgnoreExisting=*/true, perms)) {
      outs() << path << ": " << ec.message() << "\n";
      return;
    }
  }

  for (auto &path : files) {
    // Normalize all paths.
    SmallString<PATH_MAX> normalizedPathStorage(path);
    if (auto ec = sys::fs::make_absolute(normalizedPathStorage)) {
      outs() << normalizedPathStorage << ": " << ec.message() << "\n";
      continue;
    }

    sys::path::remove_dots(normalizedPathStorage, /*remove_dot_dot=*/true);

    findAndRecordSymlinks(normalizedPathStorage);
    auto normalizedPath = normalizedPathStorage.str();

    // Skip the file if we have it already hashed and copied.
    if (pathToHash.count(normalizedPath))
      continue;

    if (!sys::fs::exists(normalizedPath))
      continue;

    auto bufferOrErr = MemoryBuffer::getFile(normalizedPath, /*FileSize=*/-1,
                                             /*RequiresNullTerminator=*/false);
    if (auto ec = bufferOrErr.getError())
      continue;

    // Making the file path part of the hash will reduce some of the possible
    // space savings for files with the same content, but those are rare to
    // begin with. The reason we need to include the path is to make sure that
    // the FileManager - when using the snapshot file system - recognizes
    // different files with the same content still as distinct files.
    auto &buffer = bufferOrErr.get();
    auto contentHash = xxHash64(buffer->getBuffer());
    auto filenameHash = xxHash64(normalizedPath);
    auto hash = hash_combine(filenameHash, contentHash);

    std::string hashString;
    llvm::raw_string_ostream(hashString) << format_hex(hash, 18);

    SmallString<PATH_MAX> filePath = root;
    sys::path::append(filePath, filesDirectory,
                      StringRef(hashString).drop_front(2));
    if (auto ec = sys::fs::copy_file(normalizedPath, filePath))
      continue;

    // Store the hex string, so we know if we are loading an old or new
    // snapshot.
    pathToHash[normalizedPath] = hashString;
  }

  for (auto &path : directories) {
    // Normalize all paths.
    SmallString<PATH_MAX> normalizedPath(path);
    if (auto ec = sys::fs::make_absolute(normalizedPath)) {
      outs() << normalizedPath << ": " << ec.message() << "\n";
      continue;
    }
    sys::path::remove_dots(normalizedPath, /*remove_dot_dot=*/true);

    findAndRecordSymlinks(normalizedPath);
  }
  normalizedDirectories.insert(normalizedDirectories.end(),
                               directorySet.begin(), directorySet.end());
  sort(normalizedDirectories);
  auto last =
      std::unique(normalizedDirectories.begin(), normalizedDirectories.end());
  normalizedDirectories.erase(last, normalizedDirectories.end());

  SmallString<PATH_MAX> runScript(root);
  sys::path::append(runScript, runScriptsDirectory, name);
  sys::path::replace_extension(runScript, ".yaml");
  if (auto ec = sys::fs::createUniqueFile(runScript, runScript)) {
    outs() << runScript << ": " << ec.message() << "\n";
    return;
  }

  std::error_code ec;
  raw_fd_ostream out(runScript, ec, sys::fs::OpenFlags::F_Text);
  yaml::Output yout(out);
  yout << *this;
  if (ec) {
    outs() << runScript << ": " << ec.message() << "\n";
    return;
  }

  if (isCrash) {
    outs() << "PLEASE submit a bug report to " BUG_REPORT_URL
              " and include the "
              "crash backtrace and snapshot.\n\n"
              "********************************************************\n\n"
              "PLEASE ATTACH THE FOLLOWING DIRECTORY TO THE BUG REPORT:\n"
           << root << "\n"
           << "********************************************************\n";
  } else {
    outs() << "Snapshot written to " << root << "\n";
  }

  snapshotWritten = true;
}

bool Snapshot::loadSnapshot(StringRef path_) {
  fs = new SnapshotFileSystem();

  SmallString<PATH_MAX> path(path_);
  if (auto ec = sys::fs::make_absolute(path)) {
    outs() << path_ << ": " << ec.message() << "\n";
    return false;
  }

  SmallString<PATH_MAX> runScript;
  SmallString<PATH_MAX> inputPath;
  if (path.endswith(".yaml") &&
      sys::path::parent_path(path).endswith(runScriptsDirectory)) {
    runScript = path;
    inputPath = sys::path::parent_path(sys::path::parent_path(path));
  } else {
    runScript = path;
    sys::path::append(runScript, runScriptsDirectory);
    auto findRunScript = [](StringRef directory) {
      std::error_code ec;
      for (sys::fs::directory_iterator
               it = sys::fs::directory_iterator(directory, ec),
               e;
           it != e; it.increment(ec)) {
        if (ec) {
          outs() << ec.message() << "\n";
          return std::string();
        }
        auto path = StringRef(it->path());
        if (path.endswith(".yaml"))
          return it->path();
      }
      return std::string();
    };
    runScript = findRunScript(runScript);
    inputPath = path;
  }

  if (runScript.empty()) {
    outs() << path << ": "
           << "no such runscript or snapshot directory\n";
    return false;
  }

  auto bufferOr = MemoryBuffer::getFile(runScript);
  if (auto ec = bufferOr.getError()) {
    outs() << runScript << ": " << ec.message() << "\n";
    return false;
  }

  auto buffer = (*bufferOr)->getBuffer();
  yaml::Input yin(buffer);

  yin >> *this;

  if (auto ec = yin.error()) {
    outs() << runScript << ": " << ec.message() << "\n";
    return false;
  }

  for (auto arch : context.architectures) {
    Triple target;
    target.setArchName(getArchName(arch));
    target.setVendor(Triple::Apple);
    auto platform = mapToSim(context.platform, context.architectures.hasX86());
    target.setOSName(getOSAndEnvironmentName(platform, context.osVersion));
    frontendOptions.targets.push_back(target);
  }

  // Try to infer the clang executable path for old snapshot files.
  if (driverOptions.clangExecutablePath.empty()) {
    SmallString<PATH_MAX> clangExecutablePath;
    if (!rawArgs.empty()) {
      clangExecutablePath = sys::path::parent_path(rawArgs[0]);
      sys::path::append(clangExecutablePath, "clang");
    } else if (!frontendOptions.clangResourcePath.empty()) {
      StringRef path = frontendOptions.clangResourcePath;
      while (!path.empty() && !path.endswith("bin"))
        path = sys::path::parent_path(path);
      if (!path.empty()) {
        clangExecutablePath = path;
        sys::path::append(clangExecutablePath, "clang");
      }
    }
    if (!clangExecutablePath.empty())
      driverOptions.clangExecutablePath = clangExecutablePath.str();
  }

  // Create a separate directory for the output files.
  SmallString<PATH_MAX> root(rootPath);
  if (auto ec = sys::fs::make_absolute(root)) {
    outs() << root << ": " << ec.message() << "\n";
    return false;
  }
  sys::path::remove_dots(root, /*remove_dot_dot=*/true);

  if (auto ec = sys::fs::create_directory(root)) {
    outs() << root << ": " << ec.message() << "\n";
    return false;
  }

  sys::path::append(root, name);
  if (auto ec = sys::fs::createUniqueDirectory(root, root)) {
    outs() << root << ": " << ec.message() << "\n";
    return false;
  }
  sys::path::append(root, driverOptions.outputPath);
  driverOptions.outputPath = root.str();

  sys::path::append(inputPath, filesDirectory);
  for (const auto &mapping : pathToHash) {
    SmallString<PATH_MAX> filePath = inputPath;

    std::string fileName;
    if (StringRef(mapping.second).startswith("0x"))
      // This is the new snapshot format that uses hex numbers.
      fileName = StringRef(mapping.second).drop_front(2);
    else {
      // Support old snapshot files.
      uint64_t intValue = 0;
      StringRef(mapping.second).getAsInteger(10, intValue);
      llvm::raw_string_ostream(fileName) << format_hex_no_prefix(intValue, 0);
    }

    sys::path::append(filePath, fileName);
    assert(sys::fs::exists(filePath) && "cannot find file in snapshot");

    fs->addFile(mapping.first, filePath);
  }

  for (const auto &mapping : symlinkToPath) {
    fs->addSymlink(mapping.first, mapping.second);
  }

  for (const auto &path : normalizedDirectories)
    fs->addDirectory(path);

  fs->setCurrentWorkingDirectory(workingDirectory);

  return true;
}

void Snapshot::recordRawArguments(ArrayRef<const char *> args) {
  for (const auto *arg : args)
    rawArgs.emplace_back(arg);
}

void Snapshot::recordOptions(const Options &options) {
  command = options.command;
  driverOptions = options.driverOptions;
  archiveOptions = options.archiveOptions;
  linkerOptions = options.linkerOptions;
  frontendOptions = options.frontendOptions;
  diagnosticsOptions = options.diagnosticsOptions;
  tapiOptions = options.tapiOptions;
}

void Snapshot::recordFile(StringRef path) { files.emplace_back(path); }

void Snapshot::recordDirectory(StringRef path) {
  directories.emplace_back(path);
}

TAPI_NAMESPACE_INTERNAL_END
