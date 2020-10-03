//===- tapi/Core/IntefaceFile.h - TAPI Interface File --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief A generic and abstract interface representation for linkable objects.
///        This could be an MachO executable, bundle, dylib, or text-based stub
///        file.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_INTERFACE_FILE_H
#define TAPI_CORE_INTERFACE_FILE_H

#include "tapi/Core/Architecture.h"
#include "tapi/Core/ArchitectureSet.h"
#include "tapi/Core/Platform.h"
#include "tapi/Core/Target.h"
#include "tapi/Core/XPISet.h"
#include "tapi/Defines.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

namespace llvm {
namespace yaml {
template <typename T> struct MappingTraits;
}
} // namespace llvm

TAPI_NAMESPACE_INTERNAL_BEGIN

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

// clang-format off
enum FileType : unsigned {
  /// \brief Invalid file type.
  Invalid                   = 0U,

  /// \brief MachO Dynamic Library file.
  MachO_DynamicLibrary      = 1U <<  0,

  /// \brief MachO Dynamic Library Stub file.
  MachO_DynamicLibrary_Stub = 1U <<  1,

  /// \brief MachO Bundle file.
  MachO_Bundle              = 1U <<  2,

  /// \brief Text-based stub file (.tbd)
  TBD                       = 1U <<  3,

  All                       = ~0U,

  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/All)
};
// clang-format on

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &db, FileType type);

class VersionedFileType {
public:
  constexpr VersionedFileType() = default;
  constexpr VersionedFileType(FileType type, unsigned version = 0)
      : type(type), version(version) {}

  bool operator==(const FileType &type) const { return this->type == type; }

  bool operator!=(const FileType &type) const { return this->type != type; }

  bool operator==(const VersionedFileType &o) const {
    return std::tie(type, version) == std::tie(o.type, o.version);
  }

  bool operator!=(const VersionedFileType &o) const {
    return std::tie(type, version) != std::tie(o.type, o.version);
  }

  bool operator>=(const VersionedFileType &o) const {
    return std::tie(type, version) >= std::tie(o.type, o.version);
  }

  bool operator<(const VersionedFileType &o) const {
    return std::tie(type, version) < std::tie(o.type, o.version);
  }

  FileType type{FileType::Invalid};
  unsigned version{0};
};

static constexpr VersionedFileType TBDv1 = {FileType::TBD, 1};
static constexpr VersionedFileType TBDv2 = {FileType::TBD, 2};
static constexpr VersionedFileType TBDv3 = {FileType::TBD, 3};
static constexpr VersionedFileType TBDv4 = {FileType::TBD, 4};

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &db,
                                    VersionedFileType type);

using TargetList = std::vector<Target>;
class InterfaceFileRef {
public:
  InterfaceFileRef() = default;

  InterfaceFileRef(StringRef installName) : _installName(installName) {}

  InterfaceFileRef(StringRef installName, const TargetList &targets)
      : _installName(installName), _targets(targets) {}

  StringRef getInstallName() const { return _installName; };

  void addTarget(const Target &target);
  template <typename RangeT> void addTargets(RangeT &&targets) {
    for (const auto &target : targets)
      addTarget(Target(target));
  }

  using const_target_iterator = TargetList::const_iterator;
  using const_target_range = llvm::iterator_range<const_target_iterator>;
  const_target_range targets() const { return {_targets}; }

  ArchitectureSet getArchitectures() const {
    return mapToArchitectureSet(_targets);
  }

  bool operator==(const InterfaceFileRef &o) const {
    return std::tie(_installName, _targets) ==
           std::tie(o._installName, o._targets);
  }

  bool operator!=(const InterfaceFileRef &o) const {
    return std::tie(_installName, _targets) !=
           std::tie(o._installName, o._targets);
  }

  bool operator<(const InterfaceFileRef &o) const {
    return std::tie(_installName, _targets) <
           std::tie(o._installName, o._targets);
  }

private:
  std::string _installName;
  TargetList _targets;

  template <typename T> friend struct llvm::yaml::MappingTraits;
};

raw_ostream &operator<<(raw_ostream &os, const InterfaceFileRef &ref);

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &db,
                                    const InterfaceFileRef &ref);

class InterfaceFile {
public:
  InterfaceFile() : _symbols(new XPISet) {}
  InterfaceFile(std::unique_ptr<XPISet> &&symbols)
      : _symbols(std::move(symbols)) {}

  template <typename T> void setPath(T &&path) {
    _path = std::forward<T &&>(path);
  }
  const std::string &getPath() const { return _path; }

  llvm::StringRef getFileName() const {
    return llvm::sys::path::filename(_path);
  }

  void setFileType(VersionedFileType type) { _fileType = type; }
  VersionedFileType getFileType() const { return _fileType; }

  void setMemoryBuffer(std::unique_ptr<MemoryBuffer> memBuffer) {
    _buffer = std::move(memBuffer);
  }

  MemoryBufferRef getMemBufferRef() const { return _buffer->getMemBufferRef(); }

  void addDocument(std::shared_ptr<InterfaceFile> &&document) {
    _documents.emplace_back(std::move(document));
  }

  void addTarget(const Target &target);
  template <typename RangeT> void addTargets(RangeT &&targets) {
    for (const auto &target : targets)
      addTarget(Target(target));
  }

  using const_target_iterator = TargetList::const_iterator;
  using const_target_range = llvm::iterator_range<const_target_iterator>;
  const_target_range targets() const { return {_targets}; }

  using const_filtered_target_iterator =
      llvm::filter_iterator<const_target_iterator,
                            std::function<bool(const Target &)>>;
  using const_filtered_target_range =
      llvm::iterator_range<const_filtered_target_iterator>;
  const_filtered_target_range targets(ArchitectureSet architectures) const;

  PlatformSet getPlatforms() const { return mapToPlatformSet(_targets); }
  ArchitectureSet getArchitectures() const {
    return mapToArchitectureSet(_targets);
  }

  void setInstallName(StringRef installName) { _installName = installName; }
  StringRef getInstallName() const { return _installName; }

  void setCurrentVersion(PackedVersion version) { _currentVersion = version; }
  PackedVersion getCurrentVersion() const { return _currentVersion; }

  void setCompatibilityVersion(PackedVersion version) {
    _compatibilityVersion = version;
  }
  PackedVersion getCompatibilityVersion() const {
    return _compatibilityVersion;
  }

  void setSwiftABIVersion(uint8_t version) { _swiftABIVersion = version; }
  uint8_t getSwiftABIVersion() const { return _swiftABIVersion; }

  void setTwoLevelNamespace(bool v = true) { _isTwoLevelNamespace = v; }
  bool isTwoLevelNamespace() const { return _isTwoLevelNamespace; }

  void setApplicationExtensionSafe(bool v = true) { _isAppExtensionSafe = v; }
  bool isApplicationExtensionSafe() const { return _isAppExtensionSafe; }

  void setInstallAPI(bool v = true) { _isInstallAPI = v; }
  bool isInstallAPI() const { return _isInstallAPI; }

  void addParentUmbrella(const Target &target, StringRef parent);
  const std::vector<std::pair<Target, std::string>> &umbrellas() const {
    return _parentUmbrellas;
  }

  void addAllowableClient(StringRef installName, const Target &);
  const std::vector<InterfaceFileRef> &allowableClients() const {
    return _allowableClients;
  }

  void addReexportedLibrary(StringRef installName, const Target &);
  const std::vector<InterfaceFileRef> &reexportedLibraries() const {
    return _reexportedLibraries;
  }

  void addUUID(const Target &target, StringRef uuid);
  void addUUID(const Target &target, uint8_t uuid[16]);
  const std::vector<std::pair<Target, std::string>> &uuids() const {
    return _uuids;
  }
  void clearUUIDs() { _uuids.clear(); }

  void inlineFramework(std::shared_ptr<InterfaceFile> framework);

  void addSymbol(XPIKind kind, StringRef name, const Target &targets,
                 APILinkage linkage = APILinkage::Exported,
                 APIFlags flags = APIFlags::None,
                 APIAccess access = APIAccess::Unknown);

  template <typename RangeT,
            typename ElT = typename std::remove_reference<
                decltype(*std::begin(std::declval<RangeT>()))>::type>
  void addSymbol(XPIKind kind, StringRef name, RangeT &&targets,
                 APILinkage linkage = APILinkage::Exported,
                 APIFlags flags = APIFlags::None,
                 APIAccess access = APIAccess::Unknown) {
    switch (kind) {
    case XPIKind::GlobalSymbol:
      _symbols->addGlobalSymbol(name, linkage, flags, targets, access);
      break;
    case XPIKind::ObjectiveCClass:
      _symbols->addObjCClass(name, linkage, targets, access);
      break;
    case XPIKind::ObjectiveCClassEHType:
      _symbols->addObjCClassEHType(name, linkage, targets, access);
      break;
    case XPIKind::ObjectiveCInstanceVariable:
      _symbols->addObjCInstanceVariable(name, linkage, targets, access);
      break;
    }
  }

  ObjCClass *addObjCClass(StringRef name, const Target &target,
                          APILinkage linkage = APILinkage::Exported,
                          APIAccess access = APIAccess::Unknown);

  using const_symbol_range = XPISet::const_symbol_range;
  using const_filtered_symbol_range = XPISet::const_filtered_symbol_range;

  const_symbol_range symbols() const { return _symbols->symbols(); }
  const_filtered_symbol_range exports() const { return _symbols->exports(); }
  const_filtered_symbol_range undefineds() const {
    return _symbols->undefineds();
  }

  llvm::Optional<const XPI *> contains(XPIKind kind, StringRef name) const;

  llvm::Expected<std::unique_ptr<InterfaceFile>>
  extract(Architecture arch) const;
  llvm::Expected<std::unique_ptr<InterfaceFile>>
  remove(Architecture arch) const;
  llvm::Expected<std::unique_ptr<InterfaceFile>>
  merge(const InterfaceFile *otherInterface) const;

  bool removeSymbol(XPIKind kind, StringRef name);

  void printSymbols(ArchitectureSet archs = ArchitectureSet()) const;

  std::vector<std::shared_ptr<InterfaceFile>> _documents;

private:
  void printSymbolsForArch(Architecture arch) const;

  TargetList _targets;
  std::string _installName;
  PackedVersion _currentVersion;
  PackedVersion _compatibilityVersion;
  uint8_t _swiftABIVersion{0};
  VersionedFileType _fileType;
  bool _isTwoLevelNamespace{false};
  bool _isAppExtensionSafe{false};
  bool _isInstallAPI{false};
  std::vector<std::pair<Target, std::string>> _parentUmbrellas;
  std::vector<InterfaceFileRef> _allowableClients;
  std::vector<InterfaceFileRef> _reexportedLibraries;
  std::vector<std::pair<Target, std::string>> _uuids;
  std::unique_ptr<XPISet> _symbols;
  std::string _path;
  // The backing store this file was derived from. We use this as context for
  // the strings that we reference.
  std::unique_ptr<MemoryBuffer> _buffer;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_INTERFACE_FILE_H
