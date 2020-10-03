//===- tapi/Core/TextStubv4.cpp - Text Stub v4 ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the text stub file (TBD v4) reader/writer.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/InterfaceFile.h"
#include "tapi/Core/PackedVersion.h"
#include "tapi/Core/Registry.h"
#include "tapi/Core/TextStub.h"
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

struct UUIDv4 {
  Target target;
  std::string value;

  UUIDv4() = default;
  UUIDv4(const Target &target, const std::string &value)
      : target(target), value(value) {}
};

struct SymbolSection {
  std::vector<Target> targets;
  std::vector<FlowStringRef> symbols;
  std::vector<FlowStringRef> classes;
  std::vector<FlowStringRef> classEHs;
  std::vector<FlowStringRef> ivars;
  std::vector<FlowStringRef> weakSymbols;
  std::vector<FlowStringRef> tlvSymbols;
};

struct MetadataSection {
  std::vector<Target> targets;
  std::vector<FlowStringRef> values;
};

struct UmbrellaSection {
  std::vector<Target> targets;
  std::string umbrella;
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

LLVM_YAML_IS_SEQUENCE_VECTOR(SymbolSection)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(Target)
LLVM_YAML_IS_SEQUENCE_VECTOR(UUIDv4)
LLVM_YAML_IS_SEQUENCE_VECTOR(MetadataSection)
LLVM_YAML_IS_SEQUENCE_VECTOR(UmbrellaSection)

namespace llvm {
namespace yaml {

template <> struct ScalarTraits<Target> {
  static void output(const Target &value, void *, raw_ostream &os) {
    os << value.architecture << "-";
    switch (value.platform) {
    default:
      os << "unknown";
      break;
    case Platform::macOS:
      os << "macos";
      break;
    case Platform::iOS:
      os << "ios";
      break;
    case Platform::tvOS:
      os << "tvos";
      break;
    case Platform::watchOS:
      os << "watchos";
      break;
    // MARZIPAN RENAME
    case Platform::macCatalyst:
      os << "<6>";
      break;
    // MARZIPAN RENAME
    case Platform::iOSSimulator:
      os << "ios-simulator";
      break;
    case Platform::tvOSSimulator:
      os << "tvos-simulator";
      break;
    case Platform::watchOSSimulator:
      os << "watchos-simulator";
      break;
    }
  }

  static StringRef input(StringRef scalar, void *, Target &value) {
    auto result = Target::create(scalar);
    if (!result)
      return toString(result.takeError());

    value = *result;
    return {};
  }

  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template <> struct MappingTraits<UUIDv4> {
  static void mapping(IO &io, UUIDv4 &uuid) {
    io.mapRequired("target", uuid.target);
    io.mapRequired("value", uuid.value);
  }
};

template <> struct MappingTraits<SymbolSection> {
  static void mapping(IO &io, SymbolSection &section) {
    io.mapRequired("targets", section.targets);
    io.mapOptional("symbols", section.symbols);
    io.mapOptional("objc-classes", section.classes);
    io.mapOptional("objc-eh-types", section.classEHs);
    io.mapOptional("objc-ivars", section.ivars);
    io.mapOptional("weak-symbols", section.weakSymbols);
    io.mapOptional("thread-local-symbols", section.tlvSymbols);
  }
};

template <> struct MappingContextTraits<MetadataSection, unsigned> {
  static void mapping(IO &io, MetadataSection &section, unsigned &ctx) {
    io.mapRequired("targets", section.targets);
    if (ctx == 0)
      io.mapRequired("clients", section.values);
    else if (ctx == 1)
      io.mapRequired("libraries", section.values);
  }
};

template <> struct MappingTraits<UmbrellaSection> {
  static void mapping(IO &io, UmbrellaSection &section) {
    io.mapRequired("targets", section.targets);
    io.mapRequired("umbrella", section.umbrella);
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

template <> struct MappingTraits<const InterfaceFile *> {
  using SectionList = std::vector<SymbolSection>;
  struct NormalizedTBDv4 {
    explicit NormalizedTBDv4(IO &io) {}
    NormalizedTBDv4(IO &io, const InterfaceFile *&file) {
      auto ctx = reinterpret_cast<YAMLContext *>(io.getContext());
      assert(ctx);
      tbdVersion = ctx->fileType.version;
      targets.insert(targets.begin(), file->targets().begin(),
                     file->targets().end());
      for (const auto &it : file->uuids())
        uuids.emplace_back(it.first, it.second);
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

      {
        using TargetList = SmallVector<Target, 4>;
        std::map<std::string, TargetList> valueToTargetList;
        for (const auto &it : file->umbrellas())
          valueToTargetList[it.second].emplace_back(it.first);

        for (const auto &it : valueToTargetList) {
          UmbrellaSection section;
          section.targets.insert(section.targets.begin(), it.second.begin(),
                                 it.second.end());
          section.umbrella = it.first;
          parentUmbrellas.emplace_back(std::move(section));
        }
      }

      // TODO: Refactor
      {
        using TargetList = SmallVector<Target, 4>;
        std::set<TargetList> targetSet;
        std::map<const InterfaceFileRef *, TargetList> valueToTargetList;
        for (const auto &library : file->allowableClients()) {
          TargetList targets(library.targets());
          valueToTargetList[&library] = targets;
          targetSet.emplace(std::move(targets));
        }

        for (const auto &targets : targetSet) {
          MetadataSection section;
          section.targets.insert(section.targets.begin(), targets.begin(),
                                 targets.end());

          for (const auto &it : valueToTargetList) {
            if (it.second != targets)
              continue;

            section.values.emplace_back(it.first->getInstallName());
          }
          llvm::sort(section.values);
          allowableClients.emplace_back(std::move(section));
        }
      }

      {
        using TargetList = SmallVector<Target, 4>;
        std::set<TargetList> targetSet;
        std::map<const InterfaceFileRef *, TargetList> valueToTargetList;
        for (const auto &library : file->reexportedLibraries()) {
          TargetList targets(library.targets());
          valueToTargetList[&library] = targets;
          targetSet.emplace(std::move(targets));
        }

        for (const auto &targets : targetSet) {
          MetadataSection section;
          section.targets.insert(section.targets.begin(), targets.begin(),
                                 targets.end());

          for (const auto &it : valueToTargetList) {
            if (it.second != targets)
              continue;

            section.values.emplace_back(it.first->getInstallName());
          }
          llvm::sort(section.values);
          reexportedLibraries.emplace_back(std::move(section));
        }
      }

      auto handleSymbols =
          [](SectionList &sections,
             InterfaceFile::const_filtered_symbol_range symbols,
             std::function<bool(const XPI *)> pred) {
            using TargetList = SmallVector<Target, 4>;
            std::set<TargetList> targetSet;
            std::map<const XPI *, TargetList> symbolToTargetList;
            for (const auto *symbol : symbols) {
              if (!pred(symbol))
                continue;
              TargetList targets(symbol->targets());
              symbolToTargetList[symbol] = targets;
              targetSet.emplace(std::move(targets));
            }
            for (const auto &targets : targetSet) {
              SymbolSection section;
              section.targets.insert(section.targets.begin(), targets.begin(),
                                     targets.end());

              for (const auto &it : symbolToTargetList) {
                if (it.second != targets)
                  continue;

                const auto *symbol = it.first;
                switch (symbol->getKind()) {
                case XPIKind::GlobalSymbol:
                  if (symbol->isWeakDefined())
                    section.weakSymbols.emplace_back(symbol->getName());
                  else if (symbol->isThreadLocalValue())
                    section.tlvSymbols.emplace_back(symbol->getName());
                  else
                    section.symbols.emplace_back(symbol->getName());
                  break;
                case XPIKind::ObjectiveCClass:
                  section.classes.emplace_back(symbol->getName());
                  break;
                case XPIKind::ObjectiveCClassEHType:
                  section.classEHs.emplace_back(symbol->getName());
                  break;
                case XPIKind::ObjectiveCInstanceVariable:
                  section.ivars.emplace_back(symbol->getName());
                  break;
                }
              }
              llvm::sort(section.symbols);
              llvm::sort(section.classes);
              llvm::sort(section.classEHs);
              llvm::sort(section.ivars);
              llvm::sort(section.weakSymbols);
              llvm::sort(section.tlvSymbols);
              sections.emplace_back(std::move(section));
            }
          };

      handleSymbols(exports, file->exports(),
                    [](const XPI *xpi) { return !xpi->isReexported(); });
      handleSymbols(reexports, file->exports(),
                    [](const XPI *xpi) { return xpi->isReexported(); });
      handleSymbols(undefineds, file->undefineds(),
                    [](const XPI *xpi) { return true; });
    }

    const InterfaceFile *denormalize(IO &io) {
      auto ctx = reinterpret_cast<YAMLContext *>(io.getContext());
      assert(ctx);

      auto *file = new InterfaceFile;
      file->setPath(ctx->path);
      file->setFileType(ctx->fileType);
      for (auto &id : uuids)
        file->addUUID(id.target, id.value);
      file->addTargets(targets);
      file->setInstallName(installName);
      file->setCurrentVersion(currentVersion);
      file->setCompatibilityVersion(compatibilityVersion);
      file->setSwiftABIVersion(swiftVersion);
      for (const auto &section : parentUmbrellas)
        for (const auto &target : section.targets)
          file->addParentUmbrella(target, section.umbrella);
      file->setTwoLevelNamespace(!(flags & Flags::FlatNamespace));
      file->setApplicationExtensionSafe(
          !(flags & Flags::NotApplicationExtensionSafe));
      file->setInstallAPI(flags & Flags::InstallAPI);

      for (const auto &section : allowableClients) {
        for (const auto &lib : section.values)
          for (const auto &target : section.targets)
            file->addAllowableClient(lib, target);
      }

      for (const auto &section : reexportedLibraries) {
        for (const auto &lib : section.values)
          for (const auto &target : section.targets)
            file->addReexportedLibrary(lib, target);
      }

      // Skip symbols if requested.
      if (ctx->readFlags < ReadFlags::Symbols)
        return file;

      auto handleSymbols = [file](const SectionList &sections,
                                  APILinkage linkage = APILinkage::Exported) {
        for (const auto &section : sections) {
          for (auto &sym : section.symbols)
            file->addSymbol(XPIKind::GlobalSymbol, sym, section.targets,
                            linkage);

          for (auto &sym : section.classes)
            file->addSymbol(XPIKind::ObjectiveCClass, sym, section.targets,
                            linkage);

          for (auto &sym : section.classEHs)
            file->addSymbol(XPIKind::ObjectiveCClassEHType, sym,
                            section.targets, linkage);

          for (auto &sym : section.ivars)
            file->addSymbol(XPIKind::ObjectiveCInstanceVariable, sym,
                            section.targets, linkage);

          auto flags = linkage == APILinkage::Exported
                           ? APIFlags::WeakReferenced
                           : APIFlags::WeakDefined;
          for (auto &sym : section.weakSymbols)
            file->addSymbol(XPIKind::GlobalSymbol, sym, section.targets,
                            linkage, flags);
          for (auto &sym : section.tlvSymbols)
            file->addSymbol(XPIKind::GlobalSymbol, sym, section.targets,
                            linkage, APIFlags::ThreadLocalValue);
        }
      };

      handleSymbols(exports);
      handleSymbols(reexports, APILinkage::Reexported);
      handleSymbols(undefineds, APILinkage::External);

      return file;
    }

    unsigned tbdVersion;
    std::vector<UUIDv4> uuids;
    std::vector<Target> targets;
    StringRef installName;
    PackedVersion currentVersion;
    PackedVersion compatibilityVersion;
    SwiftVersion swiftVersion{0};
    std::vector<MetadataSection> allowableClients;
    std::vector<MetadataSection> reexportedLibraries;
    Flags flags{Flags::None};
    std::vector<UmbrellaSection> parentUmbrellas;
    SectionList exports;
    SectionList reexports;
    SectionList undefineds;
  };

  static void mappingTBDv4(IO &io, const InterfaceFile *&file) {
    const auto *ctx = reinterpret_cast<YAMLContext *>(io.getContext());
    (void)ctx;
    assert((!ctx || ctx && ctx->fileType != FileType::Invalid) &&
           "File type is not set in YAML context");
    assert(ctx->fileType >= TBDv4 && "unexpectd file type");
    MappingNormalization<NormalizedTBDv4, const InterfaceFile *> keys(io, file);

    io.mapTag("!tapi-tbd", true);
    io.mapRequired("tbd-version", keys->tbdVersion);
    io.mapRequired("targets", keys->targets);
    io.mapOptional("uuids", keys->uuids);
    io.mapOptional("flags", keys->flags, Flags::None);
    io.mapRequired("install-name", keys->installName);
    io.mapOptional("current-version", keys->currentVersion,
                   PackedVersion(1, 0, 0));
    io.mapOptional("compatibility-version", keys->compatibilityVersion,
                   PackedVersion(1, 0, 0));
    io.mapOptional("swift-abi-version", keys->swiftVersion, SwiftVersion(0));
    io.mapOptional("parent-umbrella", keys->parentUmbrellas);
    unsigned c = 0;
    io.mapOptionalWithContext("allowable-clients", keys->allowableClients, c);
    c = 1;
    io.mapOptionalWithContext("reexported-libraries", keys->reexportedLibraries,
                              c);
    io.mapOptional("exports", keys->exports);
    io.mapOptional("reexports", keys->reexports);
    io.mapOptional("undefineds", keys->undefineds);
  }
}; // namespace yaml

} // namespace yaml
} // end namespace llvm.

TAPI_NAMESPACE_INTERNAL_BEGIN

namespace stub {
namespace v4 {

bool YAMLDocumentHandler::canRead(MemoryBufferRef memBufferRef,
                                  FileType types) const {
  if (!(types & FileType::TBD))
    return false;

  auto str = memBufferRef.getBuffer().trim();
  if (!str.startswith("--- !tapi-tbd\n") || !str.endswith("..."))
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
  if (fileType != TBDv4)
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
    if (!io.mapTag("!tapi-tbd"))
      return false;
  }

  ctx->fileType = TBDv4;
  MappingTraits<const InterfaceFile *>::mappingTBDv4(io, file);

  return true;
}

} // end namespace v4.
} // end namespace stub.

TAPI_NAMESPACE_INTERNAL_END
