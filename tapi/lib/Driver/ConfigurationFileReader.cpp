//===- ConfigurationFileReader.cpp - Configuration File Reader --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the configuration file reader.
///
//===----------------------------------------------------------------------===//

#include "ConfigurationFileReader.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Core/TextStubCommon.h"
#include "tapi/Driver/ConfigurationFile.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/YAMLTraits.h"

using namespace llvm;
using namespace llvm::yaml;
using namespace TAPI_INTERNAL;
using namespace TAPI_INTERNAL::configuration::v1;

LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(Macro)
LLVM_YAML_IS_SEQUENCE_VECTOR(FrameworkConfiguration)
LLVM_YAML_IS_SEQUENCE_VECTOR(ProjectConfiguration)

namespace llvm {
namespace yaml {

template <> struct ScalarTraits<Macro> {
  static void output(const Macro &macro, void * /*unused*/, raw_ostream &out) {
    out << (macro.second ? "-U" : "-D") << macro.first;
  }

  static StringRef input(StringRef scalar, void * /*unused*/, Macro &value) {
    if (scalar.startswith("-D")) {
      value = std::make_pair(scalar.drop_front(2), false);
      return {};
    }

    if (scalar.startswith("-U")) {
      value = std::make_pair(scalar.drop_front(2), true);
      return {};
    }

    return {"invalid macro"};
  }

  static QuotingType mustQuote(StringRef /*unused*/) {
    return QuotingType::None;
  }
};

template <> struct MappingTraits<HeaderConfiguration> {
  static void mapping(IO &io, HeaderConfiguration &config) {
    io.mapOptional("umbrella", config.umbrellaHeader);
    io.mapOptional("pre-includes", config.preIncludes);
    io.mapOptional("includes", config.includes);
    io.mapOptional("excludes", config.excludes);
  }
};

template <> struct MappingTraits<FrameworkConfiguration> {
  static void mapping(IO &io, FrameworkConfiguration &config) {
    io.mapRequired("name", config.name);
    io.mapRequired("path", config.path);
    io.mapOptional("install-name", config.installName);
    io.mapOptional("language", config.language, defaultLanguage);
    io.mapOptional("include-paths", config.includePaths);
    io.mapOptional("framework-paths", config.frameworkPaths);
    io.mapOptional("macros", config.macros);
    io.mapOptional("public-header", config.publicHeaderConfiguration);
    io.mapOptional("private-header", config.privateHeaderConfiguration);
  }
};

template <> struct MappingTraits<ProjectConfiguration> {
  static void mapping(IO &io, ProjectConfiguration &config) {
    io.mapRequired("name", config.name);
    io.mapOptional("language", config.language, defaultLanguage);
    io.mapOptional("include-paths", config.includePaths);
    io.mapOptional("framework-paths", config.frameworkPaths);
    io.mapOptional("macros", config.macros);
    io.mapOptional("iosmac", config.isiOSMac);
    io.mapOptional("use-overlay", config.useOverlay);
    io.mapOptional("iosmac-umbrella-only", config.useUmbrellaOnly);
    io.mapOptional("public-header", config.publicHeaderConfiguration);
    io.mapOptional("private-header", config.privateHeaderConfiguration);
  }
};

template <> struct MappingTraits<ConfigurationFile> {
  static void mapping(IO &io, ConfigurationFile &file) {
    io.mapTag("tapi-configuration-v1", true);
    io.mapOptional("sdk-platform", file.platform, Platform::unknown);
    io.mapOptional("sdk-version", file.version);
    io.mapOptional("sdk-root", file.isysroot);
    io.mapOptional("language", file.language, clang::InputKind::ObjC);
    io.mapOptional("include-paths", file.includePaths);
    io.mapOptional("framework-paths", file.frameworkPaths);
    io.mapOptional("public-dylibs", file.publicDylibs);
    io.mapOptional("macros", file.macros);
    io.mapOptional("frameworks", file.frameworkConfigurations);
    io.mapOptional("projects", file.projectConfigurations);
  }
};

} // end namespace yaml.
} // end namespace llvm.

TAPI_NAMESPACE_INTERNAL_BEGIN

class ConfigurationFileReader::Implementation {
public:
  std::unique_ptr<MemoryBuffer> inputBuffer;
  ConfigurationFile configFile;
  Error parse(StringRef input);
};

Error ConfigurationFileReader::Implementation::parse(StringRef input) {
  auto str = input.trim();
  if (!(str.startswith("---\n") ||
        str.startswith("--- !tapi-configuration-v1\n")) ||
      !str.endswith("..."))
    return make_error<StringError>("invalid input file",
                                   inconvertibleErrorCode());

  yaml::Input yin(input);
  yin >> configFile;

  if (yin.error())
    return make_error<StringError>("malformed file\n", yin.error());
  return Error::success();
}

ConfigurationFileReader::ConfigurationFileReader(
    std::unique_ptr<MemoryBuffer> inputBuffer, Error &error)
    : impl(*new ConfigurationFileReader::Implementation()) {
  ErrorAsOutParameter errorAsOutParam(&error);
  impl.inputBuffer = std::move(inputBuffer);

  error = impl.parse(impl.inputBuffer->getBuffer());
}

Expected<std::unique_ptr<ConfigurationFileReader>>
ConfigurationFileReader::get(std::unique_ptr<MemoryBuffer> inputBuffer) {
  Error error = Error::success();
  std::unique_ptr<ConfigurationFileReader> reader(
      new ConfigurationFileReader(std::move(inputBuffer), error));
  if (error)
    return std::move(error);

  return reader;
}

ConfigurationFileReader::~ConfigurationFileReader() { delete &impl; }

ConfigurationFile ConfigurationFileReader::takeConfigurationFile() {
  return std::move(impl.configFile);
}

TAPI_NAMESPACE_INTERNAL_END
