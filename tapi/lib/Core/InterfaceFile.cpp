//===- lib/Core/InterfaceFile.cpp - Interface File --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the Interface File
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/InterfaceFile.h"
#include "tapi/Core/Path.h"
#include "tapi/Core/TapiError.h"
#include "tapi/Core/XPI.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/Support/ErrorHandling.h"
#include <iomanip>
#include <sstream>

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &db,
                                    FileType type) {
  std::string name;
  switch (type) {
  case FileType::TBD:
    name = "tbd";
    break;
  case FileType::MachO_Bundle:
    name = "mach-o bundle";
    break;
  case FileType::MachO_DynamicLibrary:
    name = "mach-o dynamic library";
    break;
  case FileType::MachO_DynamicLibrary_Stub:
    name = "mach-o dynamic library stub";
    break;
  case FileType::Invalid:
  case FileType::All:
    llvm_unreachable("unexpected file type for diagnostics");
    break;
  }
  db.AddString(name);
  return db;
}

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &db,
                                    VersionedFileType v) {
  std::string name;
  switch (v.type) {
  case FileType::TBD:
    name = "tbd-v" + std::to_string(v.version);
    break;
  case FileType::MachO_Bundle:
    name = "mach-o bundle";
    break;
  case FileType::MachO_DynamicLibrary:
    name = "mach-o dynamic library";
    break;
  case FileType::MachO_DynamicLibrary_Stub:
    name = "mach-o dynamic library stub";
    break;
  case FileType::Invalid:
  case FileType::All:
    llvm_unreachable("unexpected file type for diagnostics");
    break;
  }
  db.AddString(name);
  return db;
}

namespace {
template <typename C>
typename C::iterator addEntry(C &container, StringRef installName) {
  auto it = lower_bound(container, installName,
                        [](const InterfaceFileRef &lhs, const StringRef &rhs) {
                          return lhs.getInstallName() < rhs;
                        });
  if ((it != std::end(container)) && !(installName < it->getInstallName()))
    return it;

  return container.emplace(it, installName);
}

template <typename C>
typename C::iterator addEntry(C &container, const Target &target) {
  auto it =
      lower_bound(container, target, [](const Target &lhs, const Target &rhs) {
        return lhs < rhs;
      });
  if ((it != std::end(container)) && !(target < *it))
    return it;

  return container.emplace(it, target);
}
} // end anonymous namespace.

void InterfaceFileRef::addTarget(const Target &target) {
  addEntry(_targets, target);
}

raw_ostream &operator<<(raw_ostream &os, const InterfaceFileRef &ref) {
  os << ref.getInstallName() << " [ " << ref.getArchitectures() << " ]";
  return os;
}

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &db,
                                    const InterfaceFileRef &ref) {
  auto str = ref.getInstallName().str() + " [ " +
             std::string(ref.getArchitectures()) + " ]";
  db.AddString(str);
  return db;
}

void InterfaceFile::addTarget(const Target &target) {
  addEntry(_targets, target);
}

InterfaceFile::const_filtered_target_range
InterfaceFile::targets(ArchitectureSet architectures) const {
  std::function<bool(const Target &)> fn =
      [architectures](const Target &target) {
        return architectures.has(target.architecture);
      };
  return make_filter_range(_targets, fn);
}

void InterfaceFile::addAllowableClient(StringRef installName,
                                       const Target &target) {
  auto lib = addEntry(_allowableClients, installName);
  lib->addTarget(target);
}

void InterfaceFile::addReexportedLibrary(StringRef installName,
                                         const Target &target) {
  auto lib = addEntry(_reexportedLibraries, installName);
  lib->addTarget(target);
}

void InterfaceFile::addParentUmbrella(const Target &target,
                                      StringRef umbrella) {
  if (umbrella.empty())
    return;
  auto it = lower_bound(_parentUmbrellas, target,
                        [](const std::pair<Target, std::string> &lhs,
                           Target rhs) { return lhs.first < rhs; });

  if ((it != _parentUmbrellas.end()) && !(target < it->first)) {
    it->second = umbrella;
    return;
  }

  _parentUmbrellas.emplace(it, target, umbrella);
  return;
}

void InterfaceFile::addUUID(const Target &target, StringRef uuid) {
  auto it = lower_bound(_uuids, target,
                        [](const std::pair<Target, std::string> &lhs,
                           Target rhs) { return lhs.first < rhs; });

  if ((it != _uuids.end()) && !(target < it->first)) {
    it->second = uuid;
    return;
  }

  _uuids.emplace(it, target, uuid);
  return;
}

void InterfaceFile::addUUID(const Target &target, uint8_t uuid[16]) {
  std::stringstream stream;
  for (unsigned i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      stream << '-';
    stream << std::setfill('0') << std::setw(2) << std::uppercase << std::hex
           << static_cast<int>(uuid[i]);
  }
  addUUID(target, stream.str());
}

void InterfaceFile::inlineFramework(std::shared_ptr<InterfaceFile> framework) {
  auto addFramework = [&](std::shared_ptr<InterfaceFile> &&framework) {
    auto it = lower_bound(
        _documents, framework->getInstallName(),
        [](std::shared_ptr<InterfaceFile> &lhs, const std::string &rhs) {
          return lhs->getInstallName() < rhs;
        });

    if ((it != _documents.end()) &&
        !(framework->getInstallName() < (*it)->getInstallName()))
      return;

    _documents.emplace(it, std::move(framework));
  };
  for (auto &doc : framework->_documents)
    addFramework(std::move(doc));

  framework->_documents.clear();
  addFramework(std::move(framework));
}

void InterfaceFile::addSymbol(XPIKind kind, StringRef name,
                              const Target &target, APILinkage linkage,
                              APIFlags flags, APIAccess access) {
  switch (kind) {
  case XPIKind::GlobalSymbol:
    _symbols->addGlobalSymbol(name, linkage, flags, target, access);
    break;
  case XPIKind::ObjectiveCClass:
    _symbols->addObjCClass(name, linkage, target, access);
    break;
  case XPIKind::ObjectiveCClassEHType:
    _symbols->addObjCClassEHType(name, linkage, target, access);
    break;
  case XPIKind::ObjectiveCInstanceVariable:
    _symbols->addObjCInstanceVariable(name, linkage, target, access);
    break;
  }
}

ObjCClass *InterfaceFile::addObjCClass(StringRef name, const Target &target,
                                       APILinkage linkage, APIAccess access) {
  return _symbols->addObjCClass(name, linkage, target, access);
}

Optional<const XPI *> InterfaceFile::contains(XPIKind kind,
                                              StringRef name) const {
  if (auto *xpi = _symbols->findSymbol(kind, name))
    return xpi;

  return llvm::None;
}

Expected<std::unique_ptr<InterfaceFile>>
InterfaceFile::extract(Architecture arch) const {
  if (!getArchitectures().has(arch)) {
    return make_error<StringError>("file doesn't have architecture '" +
                                       getArchName(arch) + "'",
                                   inconvertibleErrorCode());
  }

  std::unique_ptr<InterfaceFile> interface(new InterfaceFile());
  interface->setFileType(getFileType());
  interface->setPath(getPath());
  interface->addTargets(targets(arch));
  interface->setInstallName(getInstallName());
  interface->setCurrentVersion(getCurrentVersion());
  interface->setCompatibilityVersion(getCompatibilityVersion());
  interface->setSwiftABIVersion(getSwiftABIVersion());
  interface->setTwoLevelNamespace(isTwoLevelNamespace());
  interface->setApplicationExtensionSafe(isApplicationExtensionSafe());
  interface->setInstallAPI(isInstallAPI());
  for (const auto &it : umbrellas())
    interface->addParentUmbrella(it.first, it.second);

  for (const auto &lib : allowableClients())
    for (const auto &target : lib.targets())
      if (target.architecture == arch)
        interface->addAllowableClient(lib.getInstallName(), target);

  for (const auto &lib : reexportedLibraries())
    for (const auto &target : lib.targets())
      if (target.architecture == arch)
        interface->addReexportedLibrary(lib.getInstallName(), target);

  for (const auto &uuid : uuids())
    if (uuid.first == arch)
      interface->addUUID(uuid.first, uuid.second);

  for (const auto *symbol : symbols()) {
    interface->addSymbol(symbol->getKind(), symbol->getName(),
                         symbol->targets(arch), symbol->getLinkage(),
                         symbol->getFlags(), symbol->getAccess());
  }

  for (auto &document : _documents) {
    // Skip documents that don't have the requested architecture.
    if (!document->getArchitectures().has(arch))
      continue;

    auto result = document->extract(arch);
    if (!result)
      return result;

    interface->addDocument(std::move(result.get()));
  }

  return std::move(interface);
}

Expected<std::unique_ptr<InterfaceFile>>
InterfaceFile::remove(Architecture arch) const {
  if (getArchitectures() == arch)
    return make_error<StringError>("cannot remove last architecture slice '" +
                                       getArchName(arch) + "'",
                                   inconvertibleErrorCode());

  if (!getArchitectures().has(arch)) {
    bool foundArch = false;
    for (auto &document : _documents) {
      if (document->getArchitectures().has(arch)) {
        foundArch = true;
        break;
      }
    }

    if (!foundArch)
      return make_error<TapiError>(TapiErrorCode::NoSuchArchitecture);
  }

  std::unique_ptr<InterfaceFile> interface(new InterfaceFile());
  interface->setFileType(getFileType());
  interface->setPath(getPath());
  interface->addTargets(targets(ArchitectureSet::All().clear(arch)));
  interface->setInstallName(getInstallName());
  interface->setCurrentVersion(getCurrentVersion());
  interface->setCompatibilityVersion(getCompatibilityVersion());
  interface->setSwiftABIVersion(getSwiftABIVersion());
  interface->setTwoLevelNamespace(isTwoLevelNamespace());
  interface->setApplicationExtensionSafe(isApplicationExtensionSafe());
  interface->setInstallAPI(isInstallAPI());
  for (const auto &it : umbrellas())
    interface->addParentUmbrella(it.first, it.second);

  for (const auto &lib : allowableClients()) {
    for (const auto &target : lib.targets())
      if (target.architecture != arch)
        interface->addAllowableClient(lib.getInstallName(), target);
  }

  for (const auto &lib : reexportedLibraries()) {
    for (const auto &target : lib.targets())
      if (target.architecture != arch)
        interface->addReexportedLibrary(lib.getInstallName(), target);
  }

  for (const auto &uuid : uuids())
    if (uuid.first != arch)
      interface->addUUID(uuid.first, uuid.second);

  for (const auto *symbol : symbols()) {
    auto archs = symbol->getArchitectures();
    archs.clear(arch);
    if (archs.empty())
      continue;

    interface->addSymbol(symbol->getKind(), symbol->getName(),
                         symbol->targets(archs), symbol->getLinkage(),
                         symbol->getFlags(), symbol->getAccess());
  }

  for (auto &document : _documents) {
    // Skip the inlined document if the to be removed architecture is the only
    // one left.
    if (document->getArchitectures() == arch)
      continue;

    // If the document doesn't contain the arch, then no work is to be done and
    // it can be copied over.
    if (!document->getArchitectures().has(arch)) {
      auto newDoc = document;
      interface->addDocument(std::move(newDoc));
      continue;
    }

    auto result = document->remove(arch);
    if (!result)
      return result;

    interface->addDocument(std::move(result.get()));
  }

  return std::move(interface);
}

Expected<std::unique_ptr<InterfaceFile>>
InterfaceFile::merge(const InterfaceFile *otherInterface) const {
  // Verify files can be merged.
  if (getInstallName() != otherInterface->getInstallName()) {
    return make_error<StringError>("install names do not match",
                                   inconvertibleErrorCode());
  }

  if (getCurrentVersion() != otherInterface->getCurrentVersion()) {
    return make_error<StringError>("current versions do not match",
                                   inconvertibleErrorCode());
  }

  if (getCompatibilityVersion() != otherInterface->getCompatibilityVersion()) {
    return make_error<StringError>("compatibility versions do not match",
                                   inconvertibleErrorCode());
  }

  if ((getSwiftABIVersion() != 0) &&
      (otherInterface->getSwiftABIVersion() != 0) &&
      (getSwiftABIVersion() != otherInterface->getSwiftABIVersion())) {
    return make_error<StringError>("swift ABI versions do not match",
                                   inconvertibleErrorCode());
  }

  if (isTwoLevelNamespace() != otherInterface->isTwoLevelNamespace()) {
    return make_error<StringError>("two level namespace flags do not match",
                                   inconvertibleErrorCode());
  }

  if (isApplicationExtensionSafe() !=
      otherInterface->isApplicationExtensionSafe()) {
    return make_error<StringError>(
        "application extension safe flags do not match",
        inconvertibleErrorCode());
  }

  if (isInstallAPI() != otherInterface->isInstallAPI()) {
    return make_error<StringError>("installapi flags do not match",
                                   inconvertibleErrorCode());
  }

  std::unique_ptr<InterfaceFile> interface(new InterfaceFile());
  interface->setFileType(
      std::max(getFileType(), otherInterface->getFileType()));
  interface->setPath(getPath());
  interface->setInstallName(getInstallName());
  interface->setCurrentVersion(getCurrentVersion());
  interface->setCompatibilityVersion(getCompatibilityVersion());

  if (getSwiftABIVersion() == 0)
    interface->setSwiftABIVersion(otherInterface->getSwiftABIVersion());
  else
    interface->setSwiftABIVersion(getSwiftABIVersion());

  interface->setTwoLevelNamespace(isTwoLevelNamespace());
  interface->setApplicationExtensionSafe(isApplicationExtensionSafe());
  interface->setInstallAPI(isInstallAPI());

  for (const auto &it : umbrellas())
    interface->addParentUmbrella(it.first, it.second);
  for (const auto &it : otherInterface->umbrellas())
    interface->addParentUmbrella(it.first, it.second);
  interface->addTargets(targets());
  interface->addTargets(otherInterface->targets());

  for (const auto &lib : allowableClients())
    for (const auto &target : lib.targets())
      interface->addAllowableClient(lib.getInstallName(), target);

  for (const auto &lib : otherInterface->allowableClients())
    for (const auto &target : lib.targets())
      interface->addAllowableClient(lib.getInstallName(), target);

  for (const auto &lib : reexportedLibraries())
    for (const auto &target : lib.targets())
      interface->addReexportedLibrary(lib.getInstallName(), target);

  for (const auto &lib : otherInterface->reexportedLibraries())
    for (const auto &target : lib.targets())
      interface->addReexportedLibrary(lib.getInstallName(), target);

  for (const auto &uuid : uuids())
    interface->addUUID(uuid.first, uuid.second);

  for (const auto &uuid : otherInterface->uuids())
    interface->addUUID(uuid.first, uuid.second);

  for (const auto *symbol : symbols()) {
    interface->addSymbol(symbol->getKind(), symbol->getName(),
                         symbol->targets(), symbol->getLinkage(),
                         symbol->getFlags(), symbol->getAccess());
  }

  for (const auto *symbol : otherInterface->symbols()) {
    interface->addSymbol(symbol->getKind(), symbol->getName(),
                         symbol->targets(), symbol->getLinkage(),
                         symbol->getFlags(), symbol->getAccess());
  }

  return std::move(interface);
}

bool InterfaceFile::removeSymbol(XPIKind kind, StringRef name) {
  return _symbols->removeSymbol(kind, name);
}

void InterfaceFile::printSymbolsForArch(Architecture arch) const {
  std::vector<std::string> exports;
  for (const auto *symbol : this->exports()) {
    if (!symbol->hasArchitecture(arch))
      continue;

    switch (symbol->getKind()) {
    case XPIKind::GlobalSymbol:
      exports.emplace_back(symbol->getName());
      break;
    case XPIKind::ObjectiveCClass:
      if (getPlatforms().count(Platform::macOS) && arch == AK_i386) {
        exports.emplace_back(".objc_class_name_" + symbol->getName().str());
      } else {
        exports.emplace_back("_OBJC_CLASS_$_" + symbol->getName().str());
        exports.emplace_back("_OBJC_METACLASS_$_" + symbol->getName().str());
      }
      break;
    case XPIKind::ObjectiveCClassEHType:
      exports.emplace_back("_OBJC_EHTYPE_$_" + symbol->getName().str());
      break;
    case XPIKind::ObjectiveCInstanceVariable:
      exports.emplace_back("_OBJC_IVAR_$_" + symbol->getName().str());
      break;
    }
  }

  sort(exports);
  for (const auto &symbol : exports)
    outs() << symbol << "\n";
}

void InterfaceFile::printSymbols(ArchitectureSet archs) const {
  if (archs.empty())
    archs = getArchitectures();

  if (getArchitectures().contains(archs)) {
    bool firstItr = true;
    for (auto arch : getArchitectures()) {
      if (!archs.has(arch))
        continue;

      if (firstItr)
        firstItr = false;
      else
        outs() << "\n";
      if (archs.count() > 1)
        outs() << getPath() << " (for architecture " << arch << "):\n";
      printSymbolsForArch(arch);
    }
  } else {
    outs() << "file: " << getPath()
           << " does not contain architecture: " << archs << "\n";
  }
}

TAPI_NAMESPACE_INTERNAL_END
