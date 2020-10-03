//===- lib/Core/MachODylibReader.cpp - TAPI MachO Dylib Reader --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the object specific parts of reading the dylib files.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/MachODylibReader.h"
#include "tapi/Core/API.h"
#include "tapi/Core/APIVisitor.h"
#include "tapi/Core/InterfaceFile.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Core/MachOReader.h"
#include "tapi/Core/XPI.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/MachOUniversal.h"
#include <tuple>

using namespace llvm;
using namespace llvm::object;

TAPI_NAMESPACE_INTERNAL_BEGIN

Expected<FileType>
MachODylibReader::getFileType(file_magic magic,
                              MemoryBufferRef bufferRef) const {
  return getMachOFileType(bufferRef);
}

bool MachODylibReader::canRead(file_magic magic, MemoryBufferRef bufferRef,
                               FileType types) const {
  if (!(types & FileType::MachO_DynamicLibrary) &&
      !(types & FileType::MachO_DynamicLibrary_Stub) &&
      !(types & FileType::MachO_Bundle))
    return false;

  auto fileType = getFileType(magic, bufferRef);
  if (!fileType) {
    consumeError(fileType.takeError());
    return false;
  }

  return types & fileType.get();
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

class InterfaceFileConverter : public APIVisitor {
public:
  InterfaceFileConverter(InterfaceFile *file, const Target &target,
                         bool includeUndefs)
      : file(file), target(target), includeUndefs(includeUndefs) {}
  ~InterfaceFileConverter() override {}

  void visitGlobal(const GlobalRecord &) override;

  // No typedef and enum in binary.
private:
  InterfaceFile *file;
  const Target &target;
  bool includeUndefs;
};

void InterfaceFileConverter::visitGlobal(const GlobalRecord &record) {
  // ignore internal and unknown linkage.
  if (!record.isExported())
    return;

  if (!includeUndefs && record.linkage == APILinkage::External)
    return;

  StringRef name;
  XPIKind kind;
  std::tie(name, kind) = parseSymbol(record.name);
  file->addSymbol(kind, name, target, record.linkage, record.flags);
}

Expected<std::unique_ptr<InterfaceFile>>
MachODylibReader::readFile(std::unique_ptr<MemoryBuffer> memBuffer,
                           ReadFlags readFlags, ArchitectureSet arches) const {
  MachOParseOption option;
  option.arches = arches;
  if (readFlags < ReadFlags::ObjCMetadata)
    option.parseObjCMetadata = false;
  if (readFlags < ReadFlags::Symbols) {
    option.parseSymbolTable = false;
    option.parseUndefined = false;
  }
  if (readFlags < ReadFlags::Header)
    option.parseObjCMetadata = false;

  auto results = readMachOFile(memBuffer->getMemBufferRef(), option);
  if (!results)
    return results.takeError();

  auto file = std::unique_ptr<InterfaceFile>(new InterfaceFile);
  file->setPath(memBuffer->getBufferIdentifier());
  file->setMemoryBuffer(std::move(memBuffer));

  for (const auto &result : *results) {
    const auto &triple = result.second.getTarget();
    auto target = Target(triple);
    file->addTarget(target);
    bool includeUndefs = false;
    if (result.second.hasBinaryInfo()) {
      auto &binaryInfo = result.second.getBinaryInfo();
      file->setFileType(binaryInfo.fileType);
      if (binaryInfo.isAppExtensionSafe)
        file->setApplicationExtensionSafe();

      if (binaryInfo.isTwoLevelNamespace)
        file->setTwoLevelNamespace();
      else
        // Only record undef symbols for flat namespace dylibs.
        includeUndefs = true;

      file->setCurrentVersion(binaryInfo.currentVersion);
      file->setCompatibilityVersion(binaryInfo.compatibilityVersion);
      file->addParentUmbrella(target, binaryInfo.parentUmbrella);
      file->setSwiftABIVersion(binaryInfo.swiftABIVersion);
      if (!binaryInfo.uuid.empty())
        file->addUUID(target, binaryInfo.uuid);
      if (!binaryInfo.installName.empty())
        file->setInstallName(binaryInfo.installName);
      for (const auto &client : binaryInfo.allowableClients)
        file->addAllowableClient(client, target);
      for (const auto &lib : binaryInfo.reexportedLibraries)
        file->addReexportedLibrary(lib, target);
    }

    InterfaceFileConverter converter(file.get(), target, includeUndefs);
    result.second.visit(converter);
  }

  return file;
}

TAPI_NAMESPACE_INTERNAL_END
