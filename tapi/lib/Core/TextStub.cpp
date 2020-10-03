//===- tapi/Core/TextStub.cpp - Text Stub -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the text stub file (TBD) reader/writer.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/TextStub.h"
#include "tapi/Core/InterfaceFile.h"
#include "tapi/Core/PackedVersion.h"
#include "tapi/Core/Registry.h"
#include "tapi/Core/TextStubCommon.h"
#include "tapi/Core/YAMLReaderWriter.h"
#include "tapi/LinkerInterfaceFile.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/YAMLTraits.h"
#include <set>

using namespace llvm;
using namespace llvm::yaml;
using namespace TAPI_INTERNAL;

namespace {

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

struct ExportSection {
  std::vector<Architecture> architectures;
  std::vector<FlowStringRef> allowableClients;
  std::vector<FlowStringRef> reexportedLibraries;
  std::vector<FlowStringRef> symbols;
  std::vector<FlowStringRef> classes;
  std::vector<FlowStringRef> classEHs;
  std::vector<FlowStringRef> ivars;
  std::vector<FlowStringRef> weakDefSymbols;
  std::vector<FlowStringRef> tlvSymbols;
};

struct UndefinedSection {
  std::vector<Architecture> architectures;
  std::vector<FlowStringRef> symbols;
  std::vector<FlowStringRef> classes;
  std::vector<FlowStringRef> classEHs;
  std::vector<FlowStringRef> ivars;
  std::vector<FlowStringRef> weakRefSymbols;
};

// clang-format off
enum Flags : unsigned {
  None                         = 0U,
  FlatNamespace                = 1U << 0,
  NotApplicationExtensionSafe  = 1U << 1,
  InstallAPI                   = 1U << 2,
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/InstallAPI)
};
// clang-format on

} // end anonymous namespace.

LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(Architecture)
LLVM_YAML_IS_SEQUENCE_VECTOR(ExportSection)
LLVM_YAML_IS_SEQUENCE_VECTOR(UndefinedSection)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<ExportSection> {
  static void mapping(IO &io, ExportSection &section) {
    const auto *ctx = reinterpret_cast<YAMLContext *>(io.getContext());
    assert((!ctx || ctx && ctx->fileType != FileType::Invalid) &&
           "File type is not set in YAML context");

    io.mapRequired("archs", section.architectures);
    if (ctx->fileType == TBDv1)
      io.mapOptional("allowed-clients", section.allowableClients);
    else
      io.mapOptional("allowable-clients", section.allowableClients);
    io.mapOptional("re-exports", section.reexportedLibraries);
    io.mapOptional("symbols", section.symbols);
    io.mapOptional("objc-classes", section.classes);
    if (ctx->fileType >= TBDv3)
      io.mapOptional("objc-eh-types", section.classEHs);
    io.mapOptional("objc-ivars", section.ivars);
    io.mapOptional("weak-def-symbols", section.weakDefSymbols);
    io.mapOptional("thread-local-symbols", section.tlvSymbols);
  }
};

template <> struct MappingTraits<UndefinedSection> {
  static void mapping(IO &io, UndefinedSection &section) {
    const auto *ctx = reinterpret_cast<YAMLContext *>(io.getContext());
    assert((!ctx || ctx && ctx->fileType != FileType::Invalid) &&
           "File type is not set in YAML context");

    io.mapRequired("archs", section.architectures);
    io.mapOptional("symbols", section.symbols);
    io.mapOptional("objc-classes", section.classes);
    if (ctx->fileType >= TBDv3)
      io.mapOptional("objc-eh-types", section.classEHs);
    io.mapOptional("objc-ivars", section.ivars);
    io.mapOptional("weak-ref-symbols", section.weakRefSymbols);
  }
};

template <> struct ScalarBitSetTraits<Flags> {
  static void bitset(IO &io, Flags &flags) {
    io.bitSetCase(flags, "flat_namespace", Flags::FlatNamespace);
    io.bitSetCase(flags, "not_app_extension_safe",
                  Flags::NotApplicationExtensionSafe);
    io.bitSetCase(flags, "installapi", Flags::InstallAPI);
  }
};

template <> struct ScalarTraits<PlatformSet> {
  static void output(const PlatformSet &value, void *io, raw_ostream &out) {
    const auto *ctx = reinterpret_cast<TAPI_INTERNAL::YAMLContext *>(io);
    assert((!ctx || ctx && ctx->fileType != TAPI_INTERNAL::FileType::Invalid) &&
           "File type is not set in context");
    // Conditionally add support for iosmac and zippered, because previous TBD
    // files don't support these values. Zippered is temporary and will be
    // obsoleted by TBD v4.
    if (value.count(Platform::macOS) && value.count(Platform::macCatalyst)) {
      if (ctx && ctx->fileType >= TBDv3) {
        out << "zippered";
        return;
      }
      llvm_unreachable("bad runtime enum value");
    }

    switch (*value.begin()) {
    default:
      out << "unknown";
      return;
    case Platform::macOS:
      out << "macosx";
      return;
    case Platform::iOS:
    case Platform::iOSSimulator:
      out << "ios";
      return;
    case Platform::watchOS:
    case Platform::watchOSSimulator:
      out << "watchos";
      return;
    case Platform::tvOS:
    case Platform::tvOSSimulator:
      out << "tvos";
      return;
    case Platform::macCatalyst:
      // DON'T RENAME THIS. We need to keep existing tools that read TBD v3
      // files working.
      if (ctx && ctx->fileType >= TBDv3) {
        out << "iosmac";
        return;
      }
      llvm_unreachable("bad runtime enum value");
    }
  }

  static StringRef input(StringRef scalar, void *io, PlatformSet &value) {
    const auto *ctx = reinterpret_cast<TAPI_INTERNAL::YAMLContext *>(io);
    assert((!ctx || ctx && ctx->fileType != TAPI_INTERNAL::FileType::Invalid) &&
           "File type is not set in context");
    // Conditionally add support for iosmac and zippered, because previous TBD
    // files don't support these values. Zippered is temporary and will be
    // obsoleted by TBD v4.
    if (scalar == "zippered") {
      if (ctx && ctx->fileType >= TBDv3) {
        value.insert(Platform::macOS);
        value.insert(Platform::macCatalyst);
        return {};
      }
      return "unknown enumerated scalar";
    }

    auto platform = StringSwitch<Platform>(scalar)
                        .Case("unknown", Platform::unknown)
                        .Case("macosx", Platform::macOS)
                        .Case("ios", Platform::iOS)
                        .Case("watchos", Platform::watchOS)
                        .Case("tvos", Platform::tvOS)
                        // DON'T RENAME THIS. We need this to keep existing
                        // internal TBD v3 files working.
                        .Case("iosmac", Platform::macCatalyst)
                        .Case("maccatalyst", Platform::macCatalyst)
                        .Default(Platform::unknown);

    if (platform == Platform::macCatalyst)
      if (ctx && ctx->fileType < TBDv3)
        return "unknown enumerated scalar";

    value.insert(platform);
    return {};
  }

  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template <> struct MappingTraits<const InterfaceFile *> {
  struct NormalizedTBD {
    explicit NormalizedTBD(IO &io) {}
    NormalizedTBD(IO &io, const InterfaceFile *&file) {
      auto ctx = reinterpret_cast<YAMLContext *>(io.getContext());
      assert(ctx);
      architectures = file->getArchitectures();
      uuids = file->uuids();
      platforms = file->getPlatforms();
      installName = file->getInstallName();
      currentVersion = file->getCurrentVersion();
      compatibilityVersion = file->getCompatibilityVersion();
      swiftVersion = file->getSwiftABIVersion();

      flags = Flags::None;
      if (!file->isApplicationExtensionSafe())
        flags |= Flags::NotApplicationExtensionSafe;

      if (!file->isTwoLevelNamespace())
        flags |= Flags::FlatNamespace;

      if (file->isInstallAPI())
        flags |= Flags::InstallAPI;

      for (const auto &it : file->umbrellas()) {
        parentUmbrella = it.second;
        break;
      }

      std::set<ArchitectureSet> archSet;
      for (const auto &library : file->allowableClients())
        archSet.insert(library.getArchitectures());

      for (const auto &library : file->reexportedLibraries())
        archSet.insert(library.getArchitectures());

      std::map<const XPI *, ArchitectureSet> symbolToArchSet;
      for (const auto *symbol : file->exports()) {
        auto archs = symbol->getArchitectures();
        symbolToArchSet[symbol] = archs;
        archSet.insert(archs);
      }

      for (auto archs : archSet) {
        ExportSection section;
        section.architectures = archs;

        for (const auto &library : file->allowableClients())
          if (library.getArchitectures() == archs)
            section.allowableClients.emplace_back(library.getInstallName());

        for (const auto &library : file->reexportedLibraries())
          if (library.getArchitectures() == archs)
            section.reexportedLibraries.emplace_back(library.getInstallName());

        for (const auto &symArch : symbolToArchSet) {
          if (symArch.second != archs)
            continue;

          const auto *symbol = symArch.first;
          switch (symbol->getKind()) {
          case XPIKind::GlobalSymbol:
            if (symbol->isWeakDefined())
              section.weakDefSymbols.emplace_back(symbol->getName());
            else if (symbol->isThreadLocalValue())
              section.tlvSymbols.emplace_back(symbol->getName());
            else
              section.symbols.emplace_back(symbol->getName());
            break;
          case XPIKind::ObjectiveCClass:
            if (ctx->fileType < TBDv3)
              section.classes.emplace_back(
                  copyString("_" + symbol->getName().str()));
            else
              section.classes.emplace_back(symbol->getName());
            break;
          case XPIKind::ObjectiveCClassEHType:
            if (ctx->fileType < TBDv3)
              section.symbols.emplace_back(
                  copyString("_OBJC_EHTYPE_$_" + symbol->getName().str()));
            else
              section.classEHs.emplace_back(symbol->getName());
            break;
          case XPIKind::ObjectiveCInstanceVariable:
            if (ctx->fileType < TBDv3)
              section.ivars.emplace_back(
                  copyString("_" + symbol->getName().str()));
            else
              section.ivars.emplace_back(symbol->getName());
            break;
          }
        }
        llvm::sort(section.symbols);
        llvm::sort(section.classes);
        llvm::sort(section.classEHs);
        llvm::sort(section.ivars);
        llvm::sort(section.weakDefSymbols);
        llvm::sort(section.tlvSymbols);
        exports.emplace_back(std::move(section));
      }

      archSet.clear();
      symbolToArchSet.clear();

      for (const auto *symbol : file->undefineds()) {
        auto archs = symbol->getArchitectures();
        symbolToArchSet[symbol] = archs;
        archSet.insert(archs);
      }

      for (auto archs : archSet) {
        UndefinedSection section;
        section.architectures = archs;

        for (const auto &symArch : symbolToArchSet) {
          if (symArch.second != archs)
            continue;

          const auto *symbol = symArch.first;
          switch (symbol->getKind()) {
          case XPIKind::GlobalSymbol:
            if (symbol->isWeakReferenced())
              section.weakRefSymbols.emplace_back(symbol->getName());
            else
              section.symbols.emplace_back(symbol->getName());
            break;
          case XPIKind::ObjectiveCClass:
            if (ctx->fileType < TBDv3)
              section.classes.emplace_back(
                  copyString("_" + symbol->getName().str()));
            else
              section.classes.emplace_back(symbol->getName());
            break;
          case XPIKind::ObjectiveCClassEHType:
            if (ctx->fileType < TBDv3)
              section.symbols.emplace_back(
                  copyString("_OBJC_EHTYPE_$_" + symbol->getName().str()));
            else
              section.classEHs.emplace_back(symbol->getName());
            break;
          case XPIKind::ObjectiveCInstanceVariable:
            if (ctx->fileType < TBDv3)
              section.ivars.emplace_back(
                  copyString("_" + symbol->getName().str()));
            else
              section.ivars.emplace_back(symbol->getName());
            break;
          }
        }
        llvm::sort(section.symbols);
        llvm::sort(section.classes);
        llvm::sort(section.classEHs);
        llvm::sort(section.ivars);
        llvm::sort(section.weakRefSymbols);
        undefineds.emplace_back(std::move(section));
      }
    }

    // TBD v1 - TBD v3 files only support one platform and several
    // architectures. It is possible to have more than one platform for TBD v3
    // files if they are zippered, but the architectures don't apply to all
    // platforms. In particular we need to filter out the i386 slice from
    // platform <6>.
    std::vector<Target> synthesizeTargets(ArchitectureSet architectures,
                                          const PlatformSet &platforms) {
      std::vector<Target> targets;

      for (auto platform : platforms) {
        platform = mapToSim(platform, architectures.hasX86());

        for (const auto &architecture : architectures) {
          if ((architecture == AK_i386) && (platform == Platform::macCatalyst))
            continue;

          targets.emplace_back(architecture, platform);
        }
      }
      return targets;
    }

    const InterfaceFile *denormalize(IO &io) {
      auto ctx = reinterpret_cast<YAMLContext *>(io.getContext());
      assert(ctx);

      auto *file = new InterfaceFile;
      file->setPath(ctx->path);
      file->setFileType(ctx->fileType);
      file->addTargets(synthesizeTargets(architectures, platforms));
      for (auto &id : uuids)
        for (const auto &target : file->targets(id.first.architecture))
          file->addUUID(target, id.second);
      file->setInstallName(installName);
      file->setCurrentVersion(currentVersion);
      file->setCompatibilityVersion(compatibilityVersion);
      file->setSwiftABIVersion(swiftVersion);
      for (const auto &target : file->targets())
        file->addParentUmbrella(target, parentUmbrella);

      if (ctx->fileType == TBDv1) {
        file->setTwoLevelNamespace();
        file->setApplicationExtensionSafe();
      } else {
        file->setTwoLevelNamespace(!(flags & Flags::FlatNamespace));
        file->setApplicationExtensionSafe(
            !(flags & Flags::NotApplicationExtensionSafe));
        file->setInstallAPI(flags & Flags::InstallAPI);
      }

      for (const auto &section : exports) {
        const auto targets =
            synthesizeTargets(section.architectures, platforms);

        for (const auto &lib : section.allowableClients)
          for (const auto &target : targets)
            file->addAllowableClient(lib, target);

        for (const auto &lib : section.reexportedLibraries)
          for (const auto &target : targets)
            file->addReexportedLibrary(lib, target);

        // Skip symbols if requested.
        if (ctx->readFlags < ReadFlags::Symbols)
          continue;

        for (auto &sym : section.symbols) {
          if ((ctx->fileType < TBDv3) &&
              sym.value.startswith("_OBJC_EHTYPE_$_")) {
            file->addSymbol(XPIKind::ObjectiveCClassEHType,
                            sym.value.drop_front(15), targets);
          } else {
            file->addSymbol(XPIKind::GlobalSymbol, sym, targets);
          }
        }

        for (auto &sym : section.classes) {
          auto name = sym.value;
          if (ctx->fileType < TBDv3)
            name = name.drop_front();
          file->addSymbol(XPIKind::ObjectiveCClass, name, targets);
        }

        for (auto &sym : section.classEHs)
          file->addSymbol(XPIKind::ObjectiveCClassEHType, sym, targets);

        for (auto &sym : section.ivars) {
          auto name = sym.value;
          if (ctx->fileType < TBDv3)
            name = name.drop_front();

          file->addSymbol(XPIKind::ObjectiveCInstanceVariable, name, targets);
        }

        for (auto &sym : section.weakDefSymbols)
          file->addSymbol(XPIKind::GlobalSymbol, sym, targets,
                          APILinkage::Exported, APIFlags::WeakDefined);

        for (auto &sym : section.tlvSymbols)
          file->addSymbol(XPIKind::GlobalSymbol, sym, targets,
                          APILinkage::Exported, APIFlags::ThreadLocalValue);
      }

      // Skip symbols if requested.
      if (ctx->readFlags < ReadFlags::Symbols)
        return file;

      for (const auto &section : undefineds) {
        auto targets = synthesizeTargets(section.architectures, platforms);
        for (auto &sym : section.symbols) {
          if ((ctx->fileType < TBDv3) &&
              sym.value.startswith("_OBJC_EHTYPE_$_")) {
            file->addSymbol(XPIKind::ObjectiveCClassEHType,
                            sym.value.drop_front(15), targets,
                            APILinkage::External);
          } else {
            file->addSymbol(XPIKind::GlobalSymbol, sym, targets,
                            APILinkage::External);
          }
        }

        for (auto &sym : section.classes) {
          auto name = sym.value;
          if (ctx->fileType < TBDv3)
            name = name.drop_front();

          file->addSymbol(XPIKind::ObjectiveCClass, name, targets,
                          APILinkage::External);
        }

        for (auto &sym : section.classEHs)
          file->addSymbol(XPIKind::ObjectiveCClassEHType, sym, targets,
                          APILinkage::External);

        for (auto &sym : section.ivars) {
          auto name = sym.value;
          if (ctx->fileType < TBDv3)
            name = name.drop_front();

          file->addSymbol(XPIKind::ObjectiveCInstanceVariable, name, targets,
                          APILinkage::External);
        }

        for (auto &sym : section.weakRefSymbols)
          file->addSymbol(XPIKind::GlobalSymbol, sym, targets,
                          APILinkage::External, APIFlags::WeakReferenced);
      }

      return file;
    }

    std::vector<Architecture> architectures;
    std::vector<UUID> uuids;
    PlatformSet platforms;
    StringRef installName;
    PackedVersion currentVersion;
    PackedVersion compatibilityVersion;
    SwiftVersion swiftVersion{0};
    Flags flags{Flags::None};
    StringRef parentUmbrella;
    std::vector<ExportSection> exports;
    std::vector<UndefinedSection> undefineds;

    llvm::BumpPtrAllocator allocator;
    StringRef copyString(StringRef string) {
      if (string.empty())
        return {};

      void *ptr = allocator.Allocate(string.size(), 1);
      memcpy(ptr, string.data(), string.size());
      return {reinterpret_cast<const char *>(ptr), string.size()};
    }
  };

  static void mappingTBD(IO &io, const InterfaceFile *&file) {
    const auto *ctx = reinterpret_cast<YAMLContext *>(io.getContext());
    assert((!ctx || ctx && ctx->fileType != FileType::Invalid) &&
           "File type is not set in YAML context");
    MappingNormalization<NormalizedTBD, const InterfaceFile *> keys(io, file);

    assert(ctx->fileType == FileType::TBD && "unexpected file type");
    switch (ctx->fileType.version) {
    default:
      llvm_unreachable("unexpected TBD version");
    case 1:
      // Don't write the tag into the .tbd file for TBD v1.
      if (!io.outputting())
        io.mapTag("tapi-tbd-v1", true);
      break;
    case 2:
      io.mapTag("!tapi-tbd-v2", true);
      break;
    case 3:
      io.mapTag("!tapi-tbd-v3", true);
      break;
    }

    io.mapRequired("archs", keys->architectures);
    if (ctx->fileType != TBDv1)
      io.mapOptional("uuids", keys->uuids);
    io.mapRequired("platform", keys->platforms);
    if (ctx->fileType != TBDv1)
      io.mapOptional("flags", keys->flags, Flags::None);
    io.mapRequired("install-name", keys->installName);
    io.mapOptional("current-version", keys->currentVersion,
                   PackedVersion(1, 0, 0));
    io.mapOptional("compatibility-version", keys->compatibilityVersion,
                   PackedVersion(1, 0, 0));
    if (ctx->fileType < TBDv3)
      io.mapOptional("swift-version", keys->swiftVersion, SwiftVersion(0));
    else
      io.mapOptional("swift-abi-version", keys->swiftVersion, SwiftVersion(0));
    // Don't emit the objc-constraint key anymore. It is optional and no longer
    // used.
    if (!io.outputting()) {
      ObjCConstraint dummy;
      io.mapOptional("objc-constraint", dummy);
    }
    if (ctx->fileType != TBDv1)
      io.mapOptional("parent-umbrella", keys->parentUmbrella, StringRef());
    io.mapOptional("exports", keys->exports);
    if (ctx->fileType != TBDv1)
      io.mapOptional("undefineds", keys->undefineds);
  }
}; // namespace yaml

} // namespace yaml
} // end namespace llvm.

TAPI_NAMESPACE_INTERNAL_BEGIN

namespace stub {
namespace v1 {

bool YAMLDocumentHandler::canRead(MemoryBufferRef memBufferRef,
                                  FileType types) const {
  if (!(types & FileType::TBD))
    return false;

  auto str = memBufferRef.getBuffer().trim();
  if (!(str.startswith("---\narchs:") ||
        str.startswith("--- !tapi-tbd-v1\n")) ||
      !str.endswith("..."))
    return false;

  return true;
}

FileType YAMLDocumentHandler::getFileType(MemoryBufferRef memBufferRef) const {
  if (canRead(memBufferRef))
    return FileType::TBD;

  return FileType::Invalid;
}

bool YAMLDocumentHandler::canWrite(const InterfaceFile *file,
                                   VersionedFileType fileType) const {
  if (fileType != TBDv1)
    return false;

  // TODO: report reason.
  if (!file->isApplicationExtensionSafe() || !file->isTwoLevelNamespace())
    return false;

  return true;
}

bool YAMLDocumentHandler::handleDocument(IO &io,
                                         const InterfaceFile *&file) const {
  auto *ctx = reinterpret_cast<YAMLContext *>(io.getContext());
  if (io.outputting()) {
    if (!canWrite(file, ctx->fileType))
      return false;
  } else {
    if (!io.mapTag("!tapi-tbd-v1") && !io.mapTag("tag:yaml.org,2002:map"))
      return false;
  }

  ctx->fileType = TBDv1;
  MappingTraits<const InterfaceFile *>::mappingTBD(io, file);

  return true;
}

} // end namespace v1.
} // end namespace stub.

namespace stub {
namespace v2 {

bool YAMLDocumentHandler::canRead(MemoryBufferRef memBufferRef,
                                  FileType types) const {
  if (!(types & FileType::TBD))
    return false;

  auto str = memBufferRef.getBuffer().trim();
  if (!str.startswith("--- !tapi-tbd-v2\n") || !str.endswith("..."))
    return false;

  return true;
}

FileType YAMLDocumentHandler::getFileType(MemoryBufferRef memBufferRef) const {
  if (canRead(memBufferRef))
    return FileType::TBD;

  return FileType::Invalid;
}

bool YAMLDocumentHandler::canWrite(const InterfaceFile *file,
                                   VersionedFileType fileType) const {
  if (fileType != TBDv2)
    return false;

  return true;
}

bool YAMLDocumentHandler::handleDocument(IO &io,
                                         const InterfaceFile *&file) const {
  auto *ctx = reinterpret_cast<YAMLContext *>(io.getContext());
  if (io.outputting()) {
    if (!canWrite(file, ctx->fileType))
      return false;
  } else {
    if (!io.mapTag("!tapi-tbd-v2"))
      return false;
  }

  ctx->fileType = TBDv2;
  MappingTraits<const InterfaceFile *>::mappingTBD(io, file);

  return true;
}

} // end namespace v2.
} // end namespace stub.

namespace stub {
namespace v3 {

bool YAMLDocumentHandler::canRead(MemoryBufferRef memBufferRef,
                                  FileType types) const {
  if (!(types & FileType::TBD))
    return false;

  auto str = memBufferRef.getBuffer().trim();
  if (!str.startswith("--- !tapi-tbd-v3\n") || !str.endswith("..."))
    return false;

  return true;
}

FileType YAMLDocumentHandler::getFileType(MemoryBufferRef memBufferRef) const {
  if (canRead(memBufferRef))
    return FileType::TBD;

  return FileType::Invalid;
}

bool YAMLDocumentHandler::canWrite(const InterfaceFile *file,
                                   VersionedFileType fileType) const {
  if (fileType != TBDv3)
    return false;

  return true;
}

bool YAMLDocumentHandler::handleDocument(IO &io,
                                         const InterfaceFile *&file) const {
  auto *ctx = reinterpret_cast<YAMLContext *>(io.getContext());
  if (io.outputting()) {
    if (!canWrite(file, ctx->fileType))
      return false;
  } else {
    if (!io.mapTag("!tapi-tbd-v3"))
      return false;
  }

  ctx->fileType = TBDv3;
  MappingTraits<const InterfaceFile *>::mappingTBD(io, file);

  return true;
}

} // end namespace v3.
} // end namespace stub.

TAPI_NAMESPACE_INTERNAL_END
