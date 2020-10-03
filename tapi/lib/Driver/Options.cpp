//===--- Options.cpp - Options --------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "tapi/Driver/Options.h"
#include "tapi/Config/Version.h"
#include "tapi/Core/Path.h"
#include "tapi/Defines.h"
#include "tapi/Diagnostics/Diagnostics.h"
#include "tapi/Driver/DriverOptions.h"
#include "tapi/Driver/Snapshot.h"
#include "tapi/Driver/SnapshotFileSystem.h"
#include "tapi/Driver/StatRecorder.h"
#include "tapi/LinkerInterfaceFile.h"
#include "clang/Basic/Version.inc"
#include "clang/Config/config.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <utility>

using namespace llvm;
using namespace llvm::opt;
using namespace clang;

TAPI_NAMESPACE_INTERNAL_BEGIN

constexpr char toolName[] = "Text-based Stubs Tool";

static TAPICommand getTAPICommand(StringRef value) {
  // Accept both command options (with and without "-") to not break existing
  // tools.
  return StringSwitch<TAPICommand>(value.ltrim("-"))
      .Case("archive", TAPICommand::Archive)
      .Case("stubify", TAPICommand::Stubify)
      .Case("installapi", TAPICommand::InstallAPI)
      .Case("reexport", TAPICommand::Reexport)
      .Case("generate-api-tests", TAPICommand::GenerateAPITests)
      .Default(TAPICommand::Driver);
}

static StringRef getNameFromTAPICommand(TAPICommand command) {
  switch (command) {
  case TAPICommand::Driver:
    return "";
  case TAPICommand::Archive:
    return "archive";
  case TAPICommand::Stubify:
    return "stubify";
  case TAPICommand::InstallAPI:
    return "installapi";
  case TAPICommand::Reexport:
    return "reexport";
  case TAPICommand::GenerateAPITests:
    return "generate-api-tests";
  }
}

static InputArgList parseArgString(DiagnosticsEngine &diags,
                                   ArrayRef<const char *> argString,
                                   OptTable *optTable, unsigned includedFlags,
                                   unsigned excludedFlags) {
  unsigned missingArgIndex, missingArgCount;
  auto args = optTable->ParseArgs(argString, missingArgIndex, missingArgCount,
                                  includedFlags, excludedFlags);

  // Print errors and warnings for the parsed arguments.
  if (missingArgCount) {
    diags.report(clang::diag::err_drv_missing_argument)
        << args.getArgString(missingArgIndex) << missingArgCount;
    return args;
  }

  for (auto unknownArg : args.filtered(OPT_UNKNOWN)) {
    diags.report(clang::diag::err_drv_unknown_argument)
        << unknownArg->getAsString(args);
  }

  return args;
}

static unsigned getIncludeOptionFlagMasks(TAPICommand command) {
  unsigned flags = TapiFlags::DriverOption;

  switch (command) {
  case TAPICommand::Driver:
    break;
  case TAPICommand::Archive:
    flags |= TapiFlags::ArchiveOption;
    break;
  case TAPICommand::Stubify:
    flags |= TapiFlags::StubOption;
    break;
  case TAPICommand::InstallAPI:
    flags |= TapiFlags::InstallAPIOption;
    break;
  case TAPICommand::Reexport:
    flags |= TapiFlags::ReexportOption;
    break;
  case TAPICommand::GenerateAPITests:
    flags |= TapiFlags::GenerateAPITestsOption;
    break;
  }

  return flags;
}

static std::string getTAPIExecutableDirectory() {
  // Exists solely for the purpose of lookup of the resource path.
  // This just needs to be some symbol in the binary.
  static int staticSymbol;
  auto mainExecutable = sys::fs::getMainExecutable("tapi", &staticSymbol);
  StringRef dir = llvm::sys::path::parent_path(mainExecutable);
  return dir.str();
}

static std::string getClangResourcesPath(FileManager &fm) {
  // The driver detects the builtin header path based on the path of the
  // executable.
  auto tapiDir = getTAPIExecutableDirectory();

  // Compute the path to the resource directory.

  // Try the default tapi path.
  SmallString<PATH_MAX> path(tapiDir);
  llvm::sys::path::append(path, "..", Twine("lib") + CLANG_LIBDIR_SUFFIX,
                          "tapi", TAPI_MAKE_STRING(TAPI_VERSION));
  if (fm.exists(path))
    return path.str();

  // Try the default clang path. This is used by check-tapi.
  path = tapiDir;
  llvm::sys::path::append(path, "..", Twine("lib") + CLANG_LIBDIR_SUFFIX,
                          "clang", CLANG_VERSION_STRING);
  if (fm.exists(path))
    return path.str();

  return std::string();
}

static std::string getClangExecutablePath() {
  // Try to find clang first in the toolchain. If that fails, then fall-back to
  // the default search PATH.
  auto tapiDir = getTAPIExecutableDirectory();
  auto clangBinary =
      sys::findProgramByName("clang", makeArrayRef(StringRef(tapiDir)));
  if (clangBinary.getError())
    clangBinary = sys::findProgramByName("clang");
  if (auto ec = clangBinary.getError())
    return "clang";
  else
    return clangBinary.get();
}

static std::string getTAPIConfigurationFile(FileManager &fm,
                                            StringRef platformName) {
  auto tapiDir = getTAPIExecutableDirectory();
  // Configuration directory is /usr/local/tapi/config.
  SmallString<PATH_MAX> path(tapiDir);
  llvm::sys::path::append(path, "..", "local", "tapi", "config");
  // Configuration file is named platform.conf.
  llvm::sys::path::append(path, platformName + ".conf");

  if (fm.exists(path))
    return path.str();

  return std::string();
}

bool Options::processSnapshotOptions(DiagnosticsEngine &diag,
                                     InputArgList &args) {
  // Handle --snapshot.
  auto *snapshotArg = args.getLastArg(OPT_snapshot);
  if (snapshotArg)
    snapshotOptions.snapshotMode = SnapshotMode::ForceCreate;

  // Handle --snapshot-dir=<dir>.
  if (auto *snapshotDirArg = args.getLastArg(OPT_snapshot_dir))
    snapshotOptions.snapshotOutputDir = snapshotDirArg->getValue();

  if (auto *arg = args.getLastArg(OPT_load_snapshot)) {
    if (snapshotArg) {
      diag.report(clang::diag::err_drv_argument_not_allowed_with)
          << snapshotArg->getAsString(args) << arg->getAsString(args);
      return false;
    }

    snapshotOptions.snapshotMode = SnapshotMode::Load;
    snapshotOptions.snapshotInputPath = arg->getValue();
  }

  if (::getenv("TAPI_SNAPSHOT_CREATE") != nullptr)
    snapshotOptions.snapshotMode = SnapshotMode::ForceCreate;

  if (::getenv("TAPI_USE_CC_LOG_PATH")) {
    if (auto *path = ::getenv("CC_LOG_DIAGNOSTICS_FILE")) {
      SmallString<PATH_MAX> snapshotDir(path);
      sys::path::remove_filename(snapshotDir);
      sys::path::append(snapshotDir, ".TAPI_SNAPSHOT");
      snapshotOptions.snapshotOutputDir = snapshotDir.str();
    }
  }

  if (auto *path = ::getenv("TAPI_SNAPSHOT_DIR"))
    snapshotOptions.snapshotOutputDir = path;

  if (args.getLastArg(OPT_snapshot_use_own_resource_dir))
    snapshotOptions.useOwnResourceDir = true;

  return true;
}

bool Options::processXarchOptions(DiagnosticsEngine &diag, InputArgList &args) {
  for (auto it = args.begin(), e = args.end(); it != e; ++it) {
    auto *arg = *it;
    if (!arg->getOption().matches(OPT_Xarch__))
      continue;

    auto architecture = getArchType(arg->getValue(0));
    if (architecture == AK_unknown) {
      diag.report(clang::diag::err_drv_invalid_arch_name)
          << arg->getAsString(args);
      return false;
    }

    auto nextIt = std::next(it);
    if (nextIt == e) {
      diag.report(clang::diag::err_drv_missing_argument)
          << arg->getAsString(args) << 1;
      return false;
    }
    auto *nextArg = *nextIt;
    switch ((ID)nextArg->getOption().getID()) {
    case OPT_allowable_client:
    case OPT_reexport_install_name:
    case OPT_reexport_l:
    case OPT_reexport_framework:
    case OPT_reexport_library:
      break;
    default:
      diag.report(clang::diag::err_drv_argument_not_allowed_with)
          << arg->getAsString(args) << nextArg->getAsString(args);
      return false;
    }

    argToArchMap[nextArg] = architecture;
    arg->claim();
  }

  return true;
}

/// \brief Process driver related options.
bool Options::processDriverOptions(DiagnosticsEngine &diag,
                                   InputArgList &args) {
  // Handle -version.
  if (args.hasArg(OPT_version))
    driverOptions.printVersion = true;

  // Handle help options.
  if (args.hasArg(OPT_help_hidden))
    driverOptions.printHelp = driverOptions.printHelpHidden = true;

  if (args.hasArg(OPT_help))
    driverOptions.printHelp = true;

  // Handle output file.
  SmallString<PATH_MAX> outputPath;
  if (auto *arg = args.getLastArg(OPT_output)) {
    outputPath = arg->getValue();
    if (outputPath != "-")
      fm->makeAbsolutePath(outputPath);
    driverOptions.outputPath = outputPath.str();
  }

  // Handle input files.
  if (args.hasArgNoClaim(OPT_INPUT))
    driverOptions.inputs.clear();

  for (const auto &path : args.getAllArgValues(OPT_INPUT)) {
    if (!fm->exists(path)) {
      diag.report(clang::diag::err_drv_no_such_file) << path;
      return false;
    }

    SmallString<PATH_MAX> absolutePath(path);
    fm->makeAbsolutePath(absolutePath);
    driverOptions.inputs.emplace_back(absolutePath.str());
  }

  // Handle vfs overlay paths.
  if (args.hasArgNoClaim(OPT_ivfsoverlay))
    driverOptions.vfsOverlayPaths.clear();

  for (auto *arg : args.filtered(OPT_ivfsoverlay))
    driverOptions.vfsOverlayPaths.emplace_back(arg->getValue());

  if (driverOptions.clangExecutablePath.empty()) {
    driverOptions.clangExecutablePath = getClangExecutablePath();
  }

  return true;
}

/// \brief Process archive related options.
bool Options::processArchiveOptions(DiagnosticsEngine &diag,
                                    InputArgList &args) {
  Arg *lastArg = nullptr;

  // Handle --info.
  if ((lastArg = args.getLastArg(OPT_info)))
    archiveOptions.action = ArchiveAction::ShowInfo;

  // Handle --extract <architecture>
  if (auto *arg = args.getLastArg(OPT_extract)) {
    if (lastArg) {
      diag.report(clang::diag::err_drv_argument_not_allowed_with)
          << lastArg->getAsString(args) << arg->getAsString(args);
      return false;
    }

    auto arch = getArchType(arg->getValue());
    if (arch == AK_unknown) {
      diag.report(clang::diag::err_drv_invalid_arch_name) << arg->getValue();
      return false;
    }

    archiveOptions.action = ArchiveAction::ExtractArchitecture;
    archiveOptions.arch = arch;
    lastArg = arg;
  }

  // Handle --remove <architecture>
  if (auto *arg = args.getLastArg(OPT_remove)) {
    if (lastArg) {
      diag.report(clang::diag::err_drv_argument_not_allowed_with)
          << lastArg->getAsString(args) << arg->getAsString(args);
      return false;
    }

    auto arch = getArchType(arg->getValue());
    if (arch == AK_unknown) {
      diag.report(clang::diag::err_drv_invalid_arch_name) << arg->getValue();
      return false;
    }

    archiveOptions.action = ArchiveAction::RemoveArchitecture;
    archiveOptions.arch = arch;
    lastArg = arg;
  }

  // Handle --verify-arch <architecture>.
  if (auto *arg = args.getLastArg(OPT_verify_arch)) {
    if (lastArg) {
      diag.report(clang::diag::err_drv_argument_not_allowed_with)
          << lastArg->getAsString(args) << arg->getAsString(args);
      return false;
    }

    auto arch = getArchType(arg->getValue());
    if (arch == AK_unknown) {
      diag.report(clang::diag::err_drv_invalid_arch_name) << arg->getValue();
      return false;
    }

    archiveOptions.action = ArchiveAction::VerifyArchitecture;
    archiveOptions.arch = arch;
    lastArg = arg;
  }

  // Handle --merge.
  if (auto *arg = args.getLastArg(OPT_merge)) {
    if (lastArg) {
      diag.report(clang::diag::err_drv_argument_not_allowed_with)
          << lastArg->getAsString(args) << arg->getAsString(args);
      return false;
    }

    archiveOptions.action = ArchiveAction::Merge;
  }

  // Handle --list-symbols.
  if (auto *arg = args.getLastArg(OPT_listSymbols)) {
    if (lastArg) {
      diag.report(clang::diag::err_drv_argument_not_allowed_with)
          << lastArg->getAsString(args) << arg->getAsString(args);
      return false;
    }

    archiveOptions.action = ArchiveAction::ListSymbols;
  }

  // Handle --allow-arch-merges
  if (args.hasArg(OPT_allow_arch_merges))
    archiveOptions.allowArchitectureMerges = true;

  return true;
}

/// \brief Process linker related options.
bool Options::processLinkerOptions(DiagnosticsEngine &diag,
                                   InputArgList &args) {
  // Handle dynamic lib.
  if (args.hasArg(OPT_dynamiclib))
    linkerOptions.isDynamicLibrary = true;

  // Handle install name.
  if (auto *arg = args.getLastArg(OPT_install_name))
    linkerOptions.installName = arg->getValue();

  // Handle current version.
  if (auto *arg = args.getLastArg(OPT_current_version)) {
    auto result = linkerOptions.currentVersion.parse64(arg->getValue());
    if (!result.first) {
      diag.report(diag::err_invalid_current_version) << arg->getValue();
      return false;
    }
    if (result.second)
      diag.report(diag::warn_truncating_current_version) << arg->getValue();
  }

  // Handle compatibility version.
  if (auto *arg = args.getLastArg(OPT_compatibility_version))
    if (!linkerOptions.compatibilityVersion.parse32(arg->getValue())) {
      diag.report(diag::err_invalid_compatibility_version) << arg->getValue();
      return false;
    }

  // Handle allowable clients.
  if (args.hasArgNoClaim(OPT_allowable_client))
    linkerOptions.allowableClients.clear();

  auto architectures = mapToArchitectureSet(frontendOptions.targets);
  for (auto *arg : args.filtered(OPT_allowable_client)) {
    if (argToArchMap.count(arg))
      linkerOptions.allowableClients.emplace_back(arg->getValue(),
                                                  argToArchMap[arg]);
    else
      linkerOptions.allowableClients.emplace_back(arg->getValue(),
                                                  architectures);
  }

  // Handle reexported libraries.
  if (args.hasArgNoClaim(OPT_reexport_install_name))
    linkerOptions.reexportInstallNames.clear();

  for (auto *arg : args.filtered(OPT_reexport_install_name)) {
    if (argToArchMap.count(arg))
      linkerOptions.reexportInstallNames.emplace_back(arg->getValue(),
                                                      argToArchMap[arg]);
    else
      linkerOptions.reexportInstallNames.emplace_back(arg->getValue(),
                                                      architectures);
  }

  if (args.hasArgNoClaim(OPT_reexport_l))
    linkerOptions.reexportedLibraries.clear();

  for (auto *arg : args.filtered(OPT_reexport_l)) {
    if (argToArchMap.count(arg))
      linkerOptions.reexportedLibraries.emplace_back(arg->getValue(),
                                                     argToArchMap[arg]);
    else
      linkerOptions.reexportedLibraries.emplace_back(arg->getValue(),
                                                     architectures);
  }

  if (args.hasArgNoClaim(OPT_reexport_library))
    linkerOptions.reexportedLibraryPaths.clear();

  for (auto *arg : args.filtered(OPT_reexport_library)) {
    if (argToArchMap.count(arg))
      linkerOptions.reexportedLibraryPaths.emplace_back(arg->getValue(),
                                                        argToArchMap[arg]);
    else
      linkerOptions.reexportedLibraryPaths.emplace_back(arg->getValue(),
                                                        architectures);
  }

  if (args.hasArgNoClaim(OPT_reexport_framework))
    linkerOptions.reexportedFrameworks.clear();

  for (auto *arg : args.filtered(OPT_reexport_framework)) {
    if (argToArchMap.count(arg))
      linkerOptions.reexportedFrameworks.emplace_back(arg->getValue(),
                                                      argToArchMap[arg]);
    else
      linkerOptions.reexportedFrameworks.emplace_back(arg->getValue(),
                                                      architectures);
  }

  // Handle application extension safe flag.
  if (::getenv("LD_NO_ENCRYPT") != nullptr)
    linkerOptions.isApplicationExtensionSafe = true;

  if (::getenv("LD_APPLICATION_EXTENSION_SAFE") != nullptr)
    linkerOptions.isApplicationExtensionSafe = true;

  linkerOptions.isApplicationExtensionSafe =
      args.hasFlag(OPT_fapplication_extension, OPT_fno_application_extension,
                   /*Default=*/linkerOptions.isApplicationExtensionSafe);
  for (auto *arg : args.filtered(OPT_alias_list)) {
    if (argToArchMap.count(arg))
      linkerOptions.aliasLists.emplace_back(arg->getValue(), argToArchMap[arg]);
    else
      linkerOptions.aliasLists.emplace_back(arg->getValue(), architectures);
  }

  return true;
}

/// \brief Process frontend related options.
bool Options::processFrontendOptions(DiagnosticsEngine &diag,
                                     InputArgList &args) {
  // Handle isysroot.
  if (auto *arg = args.getLastArg(OPT_isysroot)) {
    SmallString<PATH_MAX> path(arg->getValue());
    fm->makeAbsolutePath(path);
    if (!fm->exists(path)) {
      diag.report(diag::err_missing_sysroot) << path;
      return false;
    }
    frontendOptions.isysroot = path.str();
  } else if (frontendOptions.isysroot.empty()) {
    // Mirror CLANG and obtain the isysroot from the SDKROOT environment
    // variable, if it wasn't defined by the snapshot or command line.
    if (auto *env = ::getenv("SDKROOT")) {
      // Only use the SDKROOT as the default if it is an absolute path, exists,
      // and it is not the root path.
      if (llvm::sys::path::is_absolute(env) && fm->exists(env) &&
          StringRef(env) != "/")
        frontendOptions.isysroot = env;
    }
  }

  // Handle umbrella option.
  if (auto *arg = args.getLastArg(OPT_umbrella))
    frontendOptions.umbrella = arg->getValue();

  // Handle SYSTEM framework paths.
  if (args.hasArgNoClaim((OPT_iframework)))
    frontendOptions.systemFrameworkPaths.clear();

  for (auto *arg : args.filtered(OPT_iframework))
    frontendOptions.systemFrameworkPaths.emplace_back(arg->getValue());

  // Handle framework paths.
  PathSeq frameworkPaths;
  for (auto *arg : args.filtered(OPT_F))
    frameworkPaths.emplace_back(arg->getValue());

  // Handle library paths.
  PathSeq libraryPaths;
  for (auto *arg : args.filtered(OPT_L))
    libraryPaths.emplace_back(arg->getValue());

  /// Construct the search paths for libraries and frameworks.
  // Add default framework/library paths.
  PathSeq defaultLibraryPaths = {"/usr/lib", "/usr/local/lib"};
  PathSeq defaultFrameworkPaths = {"/Library/Frameworks",
                                   "/System/Library/Frameworks"};

  if (!libraryPaths.empty())
    frontendOptions.libraryPaths = libraryPaths;

  for (const auto &libPath : defaultLibraryPaths) {
    SmallString<PATH_MAX> path(frontendOptions.isysroot);
    sys::path::append(path, libPath);
    frontendOptions.libraryPaths.emplace_back(path.str());
  }

  if (!frameworkPaths.empty())
    frontendOptions.frameworkPaths = frameworkPaths;

  for (const auto &frameworkPath : defaultFrameworkPaths) {
    SmallString<PATH_MAX> path(frontendOptions.isysroot);
    sys::path::append(path, frameworkPath);
    frontendOptions.frameworkPaths.emplace_back(path.str());
  }

  // Do basic error checking first for mixing -target and -arch options.
  auto *argArch = args.getLastArgNoClaim(OPT_arch);
  auto *argTarget = args.getLastArgNoClaim(OPT_target);
  auto *argTargetVariant = args.getLastArgNoClaim(OPT_target_variant);
  if (argArch && (argTarget || argTargetVariant)) {
    diag.report(clang::diag::err_drv_argument_not_allowed_with)
        << argArch->getAsString(args)
        << (argTarget ? argTarget : argTargetVariant)->getAsString(args);
    return false;
  }

  // Clear out the target vector, because it might have been initialized by a
  // snapshot and we want to override the targets.
  if (argArch || argTarget)
    frontendOptions.targets.clear();

  // Handle -target first.
  if (argTarget) {
    for (auto *arg : args.filtered(OPT_target)) {
      Triple target(arg->getValue());
      if (target.getVendor() != Triple::Apple) {
        diag.report(diag::err_unsupported_vendor)
            << target.getVendorName() << arg->getAsString(args);
        return false;
      }

      switch (target.getOS()) {
      default:
        diag.report(diag::err_unsupported_os)
            << target.getOSName() << arg->getAsString(args);
        return false;
      case Triple::MacOSX:
      case Triple::IOS:
      case Triple::TvOS:
      case Triple::WatchOS:
        break;
      }

      switch (target.getEnvironment()) {
      default:
        diag.report(diag::err_unsupported_environment)
            << target.getEnvironmentName() << arg->getAsString(args);
        return false;
      case Triple::UnknownEnvironment:
      case Triple::MacABI:
      case Triple::Simulator:
        break;
      }
      frontendOptions.targets.push_back(target);
    }
  } else {
    // Handle deployment target.
    const std::pair<unsigned, Platform> platforms[] = {
        {OPT_mmacos_version_min_EQ, Platform::macOS},
        {OPT_mios_version_min_EQ, Platform::iOS},
        {OPT_mios_simulator_version_min_EQ, Platform::iOSSimulator},
        {OPT_mtvos_version_min_EQ, Platform::tvOS},
        {OPT_mtvos_simulator_version_min_EQ, Platform::tvOSSimulator},
        {OPT_mwatchos_version_min_EQ, Platform::watchOS},
        {OPT_mwatchos_simulator_version_min_EQ, Platform::watchOSSimulator},
        {OPT_mbridgeos_version_min_EQ, Platform::bridgeOS},
    };

    Platform platform = Platform::unknown;
    std::string osVersion;
    const Arg *first = nullptr;
    for (const auto &target : platforms) {
      auto *arg = args.getLastArg(target.first);
      if (arg == nullptr)
        continue;

      if (first != nullptr) {
        diag.report(clang::diag::err_drv_argument_not_allowed_with)
            << first->getAsString(args) << arg->getAsString(args);
        return false;
      }

      first = arg;
      platform = target.second;
      osVersion = arg->getValue();
    }

    if (platform == Platform::unknown) {
      // If no deployment target was specified on the command line, check for
      // environment defines.

      const std::pair<const char *, Platform> platforms[] = {
          {"MACOSX_DEPLOYMENT_TARGET", Platform::macOS},
          {"IPHONEOS_DEPLOYMENT_TARGET", Platform::iOS},
          {"TVOS_DEPLOYMENT_TARGET", Platform::tvOS},
          {"WATCHOS_DEPLOYMENT_TARGET", Platform::watchOS},
      };

      const char *first = nullptr;
      for (const auto &target : platforms) {
        auto *env = ::getenv(target.first);
        if (env == nullptr)
          continue;

        if (first != nullptr) {
          diag.report(clang::diag::err_drv_conflicting_deployment_targets)
              << first << target.first;
          return false;
        }

        first = target.first;
        platform = target.second;
        osVersion = env;
      }
    }

    for (auto *arg : args.filtered(OPT_arch)) {
      auto arch = getArchType(arg->getValue());
      if (arch == AK_unknown) {
        diag.report(clang::diag::err_drv_invalid_arch_name) << arg->getValue();
        return false;
      }

      Triple target;
      target.setArchName(arg->getValue());
      target.setVendor(Triple::Apple);
      target.setOSName(getOSAndEnvironmentName(platform, osVersion));
      frontendOptions.targets.push_back(target);
    }
  }

  for (auto *arg : args.filtered(OPT_target_variant)) {
    Triple variant(arg->getValue());
    if (variant.getVendor() != Triple::Apple) {
      diag.report(diag::err_unsupported_vendor)
          << variant.getVendorName() << arg->getAsString(args);
      return false;
    }

    switch (variant.getOS()) {
    default:
      diag.report(diag::err_unsupported_os)
          << variant.getOSName() << arg->getAsString(args);
      return false;
    case Triple::MacOSX:
    case Triple::IOS:
      break;
    }

    switch (variant.getEnvironment()) {
    default:
      diag.report(diag::err_unsupported_environment)
          << variant.getEnvironmentName() << arg->getAsString(args);
      return false;
    case Triple::UnknownEnvironment:
    case Triple::MacABI:
      break;
    }

    // See if there is a matching --target option for this --target-variant
    // option.
    auto it = find_if(frontendOptions.targets, [&](const Triple &target) {
      return (target.getArch() == variant.getArch()) &&
             (target.getVendor() == variant.getVendor());
    });

    if (it == frontendOptions.targets.end()) {
      diag.report(diag::err_no_matching_target) << arg->getAsString(args);
      return false;
    }

    frontendOptions.targetVariants.push_back(variant);
  }

  // Handle language option.
  if (auto *arg = args.getLastArg(OPT_x)) {
    frontendOptions.language =
        StringSwitch<clang::InputKind::Language>(arg->getValue())
            .Case("c", clang::InputKind::C)
            .Case("c++", clang::InputKind::CXX)
            .Case("objective-c", clang::InputKind::ObjC)
            .Case("objective-c++", clang::InputKind::ObjCXX)
            .Default(clang::InputKind::Unknown);

    if (frontendOptions.language == clang::InputKind::Unknown) {
      diag.report(clang::diag::err_drv_invalid_value)
          << arg->getAsString(args) << arg->getValue();
      return false;
    }
  }

  // Handle ObjC/ObjC++ switch.
  for (auto *arg : args.filtered(OPT_ObjC, OPT_ObjCXX)) {
    if (arg->getOption().matches(OPT_ObjC))
      frontendOptions.language = clang::InputKind::ObjC;
    else
      frontendOptions.language = clang::InputKind::ObjCXX;
  }

  // Handle language std.
  if (auto *arg = args.getLastArg(OPT_std_EQ))
    frontendOptions.language_std = arg->getValue();

  // Handle SYSTEM include paths.
  if (args.hasArgNoClaim(OPT_isystem))
    frontendOptions.systemIncludePaths.clear();

  for (auto *arg : args.filtered(OPT_isystem))
    frontendOptions.systemIncludePaths.emplace_back(arg->getValue());

  // Handle include paths.
  if (args.hasArgNoClaim(OPT_I))
    frontendOptions.includePaths.clear();

  for (auto *arg : args.filtered(OPT_I))
    frontendOptions.includePaths.emplace_back(arg->getValue());

  // Add macros from the command line.
  if (args.hasArgNoClaim(OPT_D) || args.hasArgNoClaim(OPT_U))
    frontendOptions.macros.clear();

  for (auto *arg : args.filtered(OPT_D, OPT_U)) {
    if (arg->getOption().matches(OPT_D))
      frontendOptions.macros.emplace_back(arg->getValue(), /*isUndef=*/false);
    else
      frontendOptions.macros.emplace_back(arg->getValue(), /*isUndef=*/true);
  }

  // Handle RTTI generation.
  if (args.hasArg(OPT_fno_rtti))
    frontendOptions.useRTTI = false;

  // Handle visibility.
  if (auto *arg = args.getLastArg(OPT_fvisibility_EQ))
    frontendOptions.visibility = arg->getValue();

  // Handle module related options.
  if (args.hasArg(OPT_fmodules))
    frontendOptions.enableModules = true;

  if (auto *arg = args.getLastArg(OPT_fmodules_cache_path))
    frontendOptions.moduleCachePath = arg->getValue();

  if (args.hasArg(OPT_fmodules_validate_system_headers))
    frontendOptions.validateSystemHeaders = true;

  // Handle extra arguments for the parser.
  if (args.hasArgNoClaim(OPT_Xparser))
    frontendOptions.clangExtraArgs.clear();

  for (auto *arg : args.filtered(OPT_Xparser))
    frontendOptions.clangExtraArgs.emplace_back(arg->getValue());

  // Handle clang resource path.
  if (frontendOptions.clangResourcePath.empty())
    frontendOptions.clangResourcePath = getClangResourcesPath(*fm);

  // Handle Objective-C ARC
  if (args.hasArg(OPT_fobjc_arc))
    frontendOptions.useObjectiveCARC = true;

  if (args.hasArg(OPT_fobjc_weak))
    frontendOptions.useObjectiveCWeakARC = true;

  if (args.hasArg(OPT_verbose))
    frontendOptions.verbose = true;

  return true;
}

/// \brief Process frontend related options.
bool Options::processDiagnosticsOptions(DiagnosticsEngine &diag,
                                        InputArgList &args) {
  // Handle diagnostics file.
  if (auto *arg = args.getLastArg(OPT__serialize_diags))
    diagnosticsOptions.serializeDiagnosticsFile = arg->getValue();

  if (auto *arg = args.getLastArg(OPT_ferror_limit)) {
    if (StringRef(arg->getValue())
            .getAsInteger(10, diagnosticsOptions.errorLimit)) {
      diag.report(clang::diag::err_drv_invalid_int_value)
          << arg->getAsString(args) << arg->getValue();
      return false;
    }
  }

  return true;
}

/// \brief Handle TAPI related options.
bool Options::processTAPIOptions(DiagnosticsEngine &diag, InputArgList &args) {
  // Handle file list.
  if (auto *arg = args.getLastArg(OPT_file_list_EQ))
    tapiOptions.fileList = arg->getValue();

  // Handle public/private umbrella header.
  if (auto *arg = args.getLastArg(OPT_public_umbrella_header))
    tapiOptions.publicUmbrellaHeaderPath = arg->getValue();

  if (auto *arg = args.getLastArg(OPT_private_umbrella_header))
    tapiOptions.privateUmbrellaHeaderPath = arg->getValue();

  auto &fm = getFileManager();
  auto addHeaderFiles = [&fm, &diag, &args](PathSeq &headers,
                                            OptSpecifier optID) {
    for (const auto &path : args.getAllArgValues(optID)) {
      if (fm.isDirectory(path, /*CacheFailure=*/false)) {
        auto result = enumerateHeaderFiles(fm, path);
        if (!result) {
          diag.report(diag::err) << path << toString(result.takeError());
          return false;
        }
        // Sort headers to ensure deterministic behavior.
        sort(*result);
        for (auto &path : *result)
          headers.emplace_back(path);
      } else
        headers.emplace_back(path);
    }

    return true;
  };

  // Handle extra header directories/files.
  if (args.hasArgNoClaim(OPT_extra_public_header))
    tapiOptions.extraPublicHeaders.clear();

  if (!addHeaderFiles(tapiOptions.extraPublicHeaders, OPT_extra_public_header))
    return false;

  if (args.hasArgNoClaim(OPT_extra_private_header))
    tapiOptions.extraPrivateHeaders.clear();

  if (!addHeaderFiles(tapiOptions.extraPrivateHeaders,
                      OPT_extra_private_header))
    return false;

  if (args.hasArgNoClaim(OPT_extra_project_header))
    tapiOptions.extraProjectHeaders.clear();

  if (!addHeaderFiles(tapiOptions.extraProjectHeaders,
                      OPT_extra_project_header))
    return false;

  // Handle excluded header files.
  if (args.hasArgNoClaim(OPT_exclude_public_header))
    tapiOptions.excludePublicHeaders.clear();

  if (!addHeaderFiles(tapiOptions.excludePublicHeaders,
                      OPT_exclude_public_header))
    return false;

  if (args.hasArgNoClaim(OPT_exclude_private_header))
    tapiOptions.excludePrivateHeaders.clear();

  if (!addHeaderFiles(tapiOptions.excludePrivateHeaders,
                      OPT_exclude_private_header))
    return false;

  if (args.hasArgNoClaim(OPT_exclude_project_header))
    tapiOptions.excludeProjectHeaders.clear();

  if (!addHeaderFiles(tapiOptions.excludeProjectHeaders,
                      OPT_exclude_project_header))
    return false;

  // Handle verify against.
  if (auto *arg = args.getLastArg(OPT_verify_against))
    tapiOptions.verifyAgainst = arg->getValue();

  // Handle verification mode.
  if (auto *arg = args.getLastArg(OPT_verify_mode_EQ)) {
    tapiOptions.verificationMode =
        StringSwitch<VerificationMode>(arg->getValue())
            .Case("ErrorsOnly", VerificationMode::ErrorsOnly)
            .Case("ErrorsAndWarnings", VerificationMode::ErrorsAndWarnings)
            .Case("Pedantic", VerificationMode::Pedantic)
            .Default(VerificationMode::Invalid);

    if (tapiOptions.verificationMode == VerificationMode::Invalid) {
      diag.report(clang::diag::err_drv_invalid_value)
          << arg->getAsString(args) << arg->getValue();
      return false;
    }
  }

  // Check if need to generate extra symbols for code coverage.
  if (args.hasArg(OPT_fprofile_instr_generate))
    tapiOptions.generateCodeCoverageSymbols = true;

  // Handel demangling.
  if (args.hasArg(OPT_demangle))
    tapiOptions.demangle = true;

  if (args.hasArg(OPT_deleteInputFile))
    tapiOptions.deleteInputFile = true;

  if (::getenv("TAPI_DELETE_INPUT_FILE") != nullptr)
    tapiOptions.deleteInputFile = true;

  if (args.hasArg(OPT_inlinePrivateFrameworks))
    tapiOptions.inlinePrivateFrameworks = true;

  if (args.hasArg(OPT_deletePrivateFrameworks))
    tapiOptions.deletePrivateFrameworks = true;

  if (args.hasArg(OPT_noUUIDs))
    tapiOptions.recordUUIDs = false;

  if (args.hasArg(OPT_setInstallAPI)) {
    tapiOptions.setInstallAPIFlag = true;
    tapiOptions.recordUUIDs = false;
  }


  auto parseFileType = [](StringRef fileType) {
    return StringSwitch<VersionedFileType>(fileType)
        .Case("tbd-v1", TBDv1)
        .Case("tbd-v2", TBDv2)
        .Case("tbd-v3", TBDv3)
        .Case("tbd-v4", TBDv4)
        .Default({FileType::Invalid});
  };

  if (auto *arg = args.getLastArg(OPT_filetype)) {
    tapiOptions.fileType = parseFileType(arg->getValue());

    if (tapiOptions.fileType == FileType::Invalid) {
      diag.report(clang::diag::err_drv_invalid_value)
          << arg->getAsString(args) << arg->getValue();
      return false;
    }
  }

  auto fileTypeEnv = ::getenv("TAPI_OUTPUT_FILE_TYPE");
  if (fileTypeEnv != nullptr) {
    tapiOptions.fileType = parseFileType(fileTypeEnv);
    if (tapiOptions.fileType == FileType::Invalid) {
      diag.report(clang::diag::err_drv_invalid_value)
          << "env TAPI_OUTPUT_FILE_TYPE" << fileTypeEnv;
      return false;
    }
  }

  if (args.hasArgNoClaim(OPT_inferIncludePaths) ||
      args.hasArgNoClaim(OPT_noInferIncludePaths))
    tapiOptions.inferIncludePaths =
        args.hasFlag(OPT_inferIncludePaths, OPT_noInferIncludePaths);

  if (auto *arg = args.getLastArg(OPT_print_after_EQ))
    tapiOptions.printAfter = arg->getValue();

  tapiOptions.verifyAPI = args.hasFlag(OPT_verify_api, OPT_no_verify_api,
                                       /*Default=*/tapiOptions.verifyAPI);

  tapiOptions.verifyAPISkipExternalHeaders =
      args.hasFlag(OPT_verify_api_skip_external_headers,
                   OPT_no_verify_api_skip_external_headers,
                   /*Default=*/tapiOptions.verifyAPISkipExternalHeaders);

  if (args.hasArg(OPT_verify_api_error_as_warning) ||
      sys::Process::GetEnv("TAPI_API_VERIFY_ERROR_AS_WARNING"))
    tapiOptions.verifyAPIErrorAsWarning = true;

  tapiOptions.verifyAPIWhitelist =
      getTAPIConfigurationFile(getFileManager(), "EquivalentTypes");

  return true;
}


void Options::initOptionsFromSnapshot(const Snapshot &snapshot) {
  command = snapshot.command;
  driverOptions = snapshot.driverOptions;
  archiveOptions = snapshot.archiveOptions;
  linkerOptions = snapshot.linkerOptions;
  frontendOptions = snapshot.frontendOptions;
  diagnosticsOptions = snapshot.diagnosticsOptions;
  tapiOptions = snapshot.tapiOptions;
}

static void updateClangResourceDirFiles(DiagnosticsEngine &diag,
                                        FileManager &fm,
                                        StringRef originalClangResourcePath,
                                        SnapshotFileSystem *vfs) {
  auto clangResourcePath = getClangResourcesPath(fm);
  if (clangResourcePath.empty())
    return;

  auto headers = enumerateHeaderFiles(fm, clangResourcePath);
  if (!headers) {
    diag.report(diag::err) << clangResourcePath
                           << toString(headers.takeError());
    return;
  }

  SmallString<PATH_MAX> snapshotRessourcePath(originalClangResourcePath);
  // Normalize path.
  if (vfs->makeAbsolute(snapshotRessourcePath))
    return;
  sys::path::remove_dots(snapshotRessourcePath, /*remove_dot_dot=*/true);

  // Replace all files in the ressource directory from the snapshot with our
  // own files.
  for (auto &header : *headers) {
    SmallString<PATH_MAX> externalPath(header);
    SmallString<PATH_MAX> srcPath(header);

    // Normalize path.
    if (fm.getVirtualFileSystem()->makeAbsolute(externalPath))
      return;
    sys::path::remove_dots(externalPath, /*remove_dot_dot=*/true);
    sys::path::replace_path_prefix(srcPath, clangResourcePath,
                                   snapshotRessourcePath);

    vfs->addFile(srcPath, externalPath);
  }
}

Options::Options(DiagnosticsEngine &diag, ArrayRef<const char *> argString) {
  // Create the default file manager for all file operations.
  fm = new FileManager(clang::FileSystemOptions(),
                       newFileSystemStatCacheFactory<StatRecorder>());

  // Record the raw arguments.
  globalSnapshot->recordRawArguments(argString);

  table.reset(createDriverOptTable());

  // Program name
  programName = sys::path::stem(argString.front());
  argString = argString.slice(1);

  // Show the umbrella help when no command was specified and no other
  // arguments were passed to tapi.
  if (argString.empty()) {
    driverOptions.printHelp = true;
    return;
  }

  command = getTAPICommand(argString.front());
  if (command != TAPICommand::Driver)
    argString = argString.slice(1);
  auto args = parseArgString(diag, argString, table.get(),
                             getIncludeOptionFlagMasks(command), 0);

  if (diag.hasErrorOccurred())
    return;

  // Process the snaphot options first. They can affect the results of the
  // following options.
  if (!processSnapshotOptions(diag, args))
    goto recordOptions;

  if (!snapshotOptions.snapshotOutputDir.empty())
    globalSnapshot->setRootPath(snapshotOptions.snapshotOutputDir);

  if (snapshotOptions.snapshotMode == SnapshotMode::Load) {
    if (!globalSnapshot->loadSnapshot(snapshotOptions.snapshotInputPath))
      return;
    initOptionsFromSnapshot(*globalSnapshot);

    // The snapshot creates a special mapping file system that we need to use to
    // access the files that are recorded in the snapshot.
    auto fs = globalSnapshot->getVirtualFileSystem();

    if (snapshotOptions.useOwnResourceDir)
      updateClangResourceDirFiles(diag, *fm.get(),
                                  frontendOptions.clangResourcePath, fs.get());

    fm = new FileManager(
        clang::FileSystemOptions{globalSnapshot->getWorkingDirectory()},
        newFileSystemStatCacheFactory<StatRecorder>(), fs);
  } else {
    if (snapshotOptions.snapshotMode == SnapshotMode::ForceCreate)
      globalSnapshot->requestSnapshot();

    globalSnapshot->setWorkingDirectory(
        fm->getVirtualFileSystem()->getCurrentWorkingDirectory().get());
  }

  // This has to happen after processing the snapshot options, but before all
  // other option processing.
  if (!processXarchOptions(diag, args))
    goto recordOptions;

  if (!processDriverOptions(diag, args))
    goto recordOptions;

  if (!processArchiveOptions(diag, args))
    goto recordOptions;

  if (!processFrontendOptions(diag, args))
    goto recordOptions;

  if (!processLinkerOptions(diag, args))
    goto recordOptions;

  if (!processDiagnosticsOptions(diag, args))
    goto recordOptions;

  if (!processTAPIOptions(diag, args))
    goto recordOptions;


recordOptions:
  globalSnapshot->recordOptions(*this);
}

/// \brief Print umbrella help for tapi.
static void printDriverHelp(bool hidden = false) {
  outs() << "OVERVIEW: " << toolName
         << "\n\n"
            "USAGE: tapi [--version][--help]\n"
            "       tapi <command> [<args>]\n\n"
            "Commands:\n"
            "  archive     Merge or thin text-based stub files\n"
            "  stubify     Create a text-based stub file from a library\n"
            "  installapi  Create a text-based stub file by scanning the "
            "header files\n"
            "  reexport    Create a linker reexport file by scanning the "
            "header files\n"
            "\n";

  outs()
      << "See 'tapi <command> --help' to read more about a specific command.\n";
}

void Options::printHelp() const {
  if (command == TAPICommand::Driver) {
    printDriverHelp(driverOptions.printHelpHidden);
    return;
  }

  table->PrintHelp(
      outs(),
      (programName + " " + getNameFromTAPICommand(command)).str().c_str(),
      toolName, /*FlagsToInclude=*/getIncludeOptionFlagMasks(command),
      /*FlagsToExclude=*/0, /*ShowAllAliases=*/false);
}

TAPI_NAMESPACE_INTERNAL_END
