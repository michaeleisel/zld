//===- tapi/Driver/Configuration.h - Configuration --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the configuration.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_CONFIGURATION_H
#define TAPI_CORE_CONFIGURATION_H

#include "tapi/Core/ArchitectureSet.h"
#include "tapi/Core/PackedVersion.h"
#include "tapi/Core/Path.h"
#include "tapi/Defines.h"
#include "tapi/Driver/ConfigurationFile.h"
#include "clang/Frontend/FrontendOptions.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include <map>
#include <string>

TAPI_NAMESPACE_INTERNAL_BEGIN

enum class HeaderType;
class DiagnosticsEngine;
class FileManager;
class Context;

struct CommandLineConfiguration {
  clang::InputKind::Language language = clang::InputKind::Unknown;
  std::string std;
  std::string isysroot;
  std::string publicUmbrellaHeaderPath;
  std::string privateUmbrellaHeaderPath;
  std::string moduleCachePath;
  std::string clangResourcePath;
  PathSeq includePaths;
  PathSeq frameworkPaths;
  std::vector<std::pair<std::string, bool /*isUndef*/>> macros;
  std::vector<std::string> clangExtraArgs;
  PathSeq extraPublicHeaders;
  PathSeq extraPrivateHeaders;
  PathSeq excludePublicHeaders;
  PathSeq excludePrivateHeaders;
  std::string visibility;
  bool useRTTI = true;
  bool scanPublicHeaders = true;
  bool scanPrivateHeaders = true;
  bool enableModules = false;
  bool validateSystemHeaders = false;
};

class Configuration {
public:
  Configuration(Context &context) : context(context) {}
  void setConfiguration(ConfigurationFile &&configFile, Context &context);

  CommandLineConfiguration& getCommandlineConfig() {
    return commandLine;
  }
  const ArchitectureSet &getArchitectures() const { return arches; }
  void setArchitectures(const ArchitectureSet &archSet) {
    arches = archSet;
  }

  std::string getSysRoot() const;
  void setRootPath(StringRef root) { rootPath = root.str(); }

  clang::InputKind::Language getLanguage(StringRef path) const;
  std::vector<Macro> getMacros(StringRef path) const;
  PathSeq getIncludePaths(StringRef path) const;
  PathSeq getFrameworkPaths(StringRef path) const;
  PathSeq getExtraHeaders(StringRef path, HeaderType type) const;
  PathSeq getPreIncludedHeaders(StringRef path, HeaderType type) const;
  PathSeq getExcludedHeaders(StringRef path, HeaderType type) const;
  std::string getUmbrellaHeader(StringRef path, HeaderType type) const;
  bool isiOSMacProject() const;
  bool isDriverKitProject() const {
    return isDriverKit;
  }
  bool useOverlay() const;
  bool useUmbrellaOnly() const;
  bool isPromotedToPublicDylib(StringRef installName) const;

private:
  Context &context;
  CommandLineConfiguration commandLine;
  ArchitectureSet arches;
  bool isiOSMac = false;
  bool isDriverKit = false;
  ConfigurationFile file;
  std::map<std::string, const configuration::v1::FrameworkConfiguration *>
      pathToConfig;
  std::unique_ptr<configuration::v1::ProjectConfiguration> projectConfig;
  std::string rootPath;

  PathSeq updateDirectories(const PathSeq &paths) const;
  PathSeq updateSDKHeaderFiles(const PathSeq &paths) const;
  PathSeq updateBinaryFiles(const PathSeq &paths) const;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_CONFIGURATION_H
