//===- lib/Core/API.cpp - TAPI API ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "tapi/Core/API.h"
#include "tapi/Core/APIVisitor.h"

using namespace llvm;
using namespace clang;

TAPI_NAMESPACE_INTERNAL_BEGIN

APILoc::APILoc(StringRef file, unsigned line, unsigned col)
    : file(file), line(line), col(col) {}

bool APILoc::isInvalid() const {
  if (loc)
    return loc->isInvalid();
  else if (file.empty())
    return true;

  return false;
}

StringRef APILoc::getFilename() const {
  if (loc)
    return loc->getFilename();
  return file;
}

unsigned APILoc::getLine() const {
  if (loc)
    return loc->getLine();
  return line;
}

unsigned APILoc::getColumn() const {
  if (loc)
    return loc->getColumn();
  return col;
}

clang::PresumedLoc APILoc::getPresumedLoc() const {
  assert(loc && "must have an underlying PresumedLoc");
  return *loc;
}

APIRecord *APIRecord::create(BumpPtrAllocator &allocator, StringRef name,
                             APILinkage linkage, APIFlags flags, APILoc loc,
                             const AvailabilityInfo &availability,
                             APIAccess access, const Decl *decl) {
  return new (allocator)
      APIRecord{name, loc, decl, availability, linkage, flags, access};
}

GlobalRecord *GlobalRecord::create(BumpPtrAllocator &allocator, StringRef name,
                                   APILinkage linkage, APIFlags flags,
                                   APILoc loc,
                                   const AvailabilityInfo &availability,
                                   APIAccess access, const Decl *decl,
                                   GVKind kind) {

  return new (allocator)
      GlobalRecord{name, flags, loc, availability, access, decl, kind, linkage};
}

EnumConstantRecord *
EnumConstantRecord::create(BumpPtrAllocator &allocator, StringRef name,
                           APILoc loc, const AvailabilityInfo &availability,
                           APIAccess access, const Decl *decl) {
  return new (allocator)
      EnumConstantRecord{name, loc, availability, access, decl};
}

ObjCMethodRecord *
ObjCMethodRecord::create(BumpPtrAllocator &allocator, StringRef name,
                         APILoc loc, const AvailabilityInfo &availability,
                         APIAccess access, bool isInstanceMethod,
                         bool isOptional, bool isDynamic, const Decl *decl) {
  return new (allocator) ObjCMethodRecord{
      name,       loc,       availability, access, isInstanceMethod,
      isOptional, isDynamic, decl};
}

ObjCPropertyRecord *
ObjCPropertyRecord::create(BumpPtrAllocator &allocator, StringRef name,
                           StringRef getterName, StringRef setterName,
                           APILoc loc, const AvailabilityInfo &availability,
                           APIAccess access, AttributeKind attributes,
                           bool isOptional, const Decl *decl) {
  return new (allocator)
      ObjCPropertyRecord{name,   getterName, setterName, loc, availability,
                         access, attributes, isOptional, decl};
}

ObjCInstanceVariableRecord *ObjCInstanceVariableRecord::create(
    BumpPtrAllocator &allocator, StringRef name, APILinkage linkage, APILoc loc,
    const AvailabilityInfo &availability, APIAccess access,
    AccessControl accessControl, const Decl *decl) {
  return new (allocator) ObjCInstanceVariableRecord{
      name, linkage, loc, availability, access, accessControl, decl};
}

ObjCInterfaceRecord *ObjCInterfaceRecord::create(
    BumpPtrAllocator &allocator, StringRef name, APILinkage linkage, APILoc loc,
    const AvailabilityInfo &availability, APIAccess access,
    StringRef superClassName, const Decl *decl) {
  return new (allocator) ObjCInterfaceRecord{
      name, linkage, loc, availability, access, superClassName, decl};
}

ObjCCategoryRecord *
ObjCCategoryRecord::create(BumpPtrAllocator &allocator, StringRef interfaceName,
                           StringRef name, APILoc loc,
                           const AvailabilityInfo &availability,
                           APIAccess access, const Decl *decl) {
  return new (allocator)
      ObjCCategoryRecord{interfaceName, name, loc, availability, access, decl};
}

ObjCProtocolRecord *
ObjCProtocolRecord::create(BumpPtrAllocator &allocator, StringRef name,
                           APILoc loc, const AvailabilityInfo &availability,
                           APIAccess access, const Decl *decl) {
  return new (allocator)
      ObjCProtocolRecord{name, loc, availability, access, decl};
}

bool API::updateAPIAccess(APIRecord *record, APIAccess access) {
  if (record->access <= access)
    return false;

  record->access = access;
  return true;
}

bool API::updateAPILinkage(APIRecord *record, APILinkage linkage) {
  if (record->linkage >= linkage)
    return false;

  record->linkage = linkage;
  return true;
}

GlobalRecord *API::addGlobal(StringRef name, APILoc loc,
                             const AvailabilityInfo &availability,
                             APIAccess access, const Decl *decl, GVKind kind,
                             APILinkage linkage, bool isWeakDefined,
                             bool isThreadLocal) {
  auto flags = APIFlags::None;
  if (isWeakDefined)
    flags |= APIFlags::WeakDefined;
  if (isThreadLocal)
    flags |= APIFlags::ThreadLocalValue;
  return addGlobal(name, flags, loc, availability, access, decl, kind, linkage);
}

GlobalRecord *API::addGlobal(StringRef name, APIFlags flags, APILoc loc,
                             const AvailabilityInfo &availability,
                             APIAccess access, const Decl *decl, GVKind kind,
                             APILinkage linkage) {
  name = copyString(name);
  auto result = globals.insert({name, nullptr});
  if (result.second) {
    auto *record = GlobalRecord::create(allocator, name, linkage, flags, loc,
                                        availability, access, decl, kind);
    result.first->second = record;
  }
  API::updateAPIAccess(result.first->second, access);
  API::updateAPILinkage(result.first->second, linkage);
  // TODO: diagnose kind difference.
  return result.first->second;
}

GlobalRecord *API::addGlobalVariable(StringRef name, APILoc loc,
                                     const AvailabilityInfo &availability,
                                     APIAccess access, const Decl *decl,
                                     APILinkage linkage, bool isWeakDefined,
                                     bool isThreadLocal) {
  return addGlobal(name, loc, availability, access, decl, GVKind::Variable,
                   linkage, isWeakDefined, isThreadLocal);
}

GlobalRecord *API::addFunction(StringRef name, APILoc loc,
                               const AvailabilityInfo &availability,
                               APIAccess access, const Decl *decl,
                               APILinkage linkage, bool isWeakDefined) {
  return addGlobal(name, loc, availability, access, decl, GVKind::Function,
                   linkage, isWeakDefined);
}

EnumConstantRecord *API::addEnumConstant(StringRef name, APILoc loc,
                                         const AvailabilityInfo &availability,
                                         APIAccess access, const Decl *decl) {
  name = copyString(name);
  auto result = enumConstants.insert({name, nullptr});
  if (result.second) {
    auto *record = EnumConstantRecord::create(allocator, name, loc,
                                              availability, access, decl);
    result.first->second = record;
  }
  return result.first->second;
}

ObjCInterfaceRecord *API::addObjCInterface(StringRef name, APILoc loc,
                                           const AvailabilityInfo &availability,
                                           APIAccess access, APILinkage linkage,
                                           StringRef superClassName,
                                           const Decl *decl) {
  name = copyString(name);
  auto result = interfaces.insert({name, nullptr});
  if (result.second) {
    superClassName = copyString(superClassName);
    auto *record =
        ObjCInterfaceRecord::create(allocator, name, linkage, loc, availability,
                                    access, superClassName, decl);
    result.first->second = record;
  }
  return result.first->second;
}

ObjCCategoryRecord *API::addObjCCategory(StringRef interfaceName,
                                         StringRef name, APILoc loc,
                                         const AvailabilityInfo &availability,
                                         APIAccess access, const Decl *decl) {
  interfaceName = copyString(interfaceName);
  name = copyString(name);
  auto result =
      categories.insert({std::make_pair(interfaceName, name), nullptr});
  if (result.second) {
    auto *record = ObjCCategoryRecord::create(allocator, interfaceName, name,
                                              loc, availability, access, decl);
    result.first->second = record;
  }

  auto it = interfaces.find(interfaceName);
  if (it != interfaces.end())
    it->second->categories.push_back(result.first->second);

  return result.first->second;
}

ObjCProtocolRecord *API::addObjCProtocol(StringRef name, APILoc loc,
                                         const AvailabilityInfo &availability,
                                         APIAccess access, const Decl *decl) {
  name = copyString(name);
  auto result = protocols.insert({name, nullptr});
  if (result.second) {
    auto *record = ObjCProtocolRecord::create(allocator, name, loc,
                                              availability, access, decl);
    result.first->second = record;
  }

  return result.first->second;
}

ObjCMethodRecord *API::addObjCMethod(ObjCContainerRecord *record,
                                     StringRef name, APILoc loc,
                                     const AvailabilityInfo &availability,
                                     APIAccess access, bool isInstanceMethod,
                                     bool isOptional, bool isDynamic,
                                     const Decl *decl) {
  name = copyString(name);
  auto *method =
      ObjCMethodRecord::create(allocator, name, loc, availability, access,
                               isInstanceMethod, isOptional, isDynamic, decl);
  record->methods.push_back(method);
  return method;
}

ObjCPropertyRecord *
API::addObjCProperty(ObjCContainerRecord *record, StringRef name,
                     StringRef getterName, StringRef setterName, APILoc loc,
                     const AvailabilityInfo &availability, APIAccess access,
                     ObjCPropertyRecord::AttributeKind attributes,
                     bool isOptional, const Decl *decl) {
  name = copyString(name);
  getterName = copyString(getterName);
  setterName = copyString(setterName);
  auto *property = ObjCPropertyRecord::create(
      allocator, name, getterName, setterName, loc, availability, access,
      attributes, isOptional, decl);
  record->properties.push_back(property);
  return property;
}

ObjCInstanceVariableRecord *API::addObjCInstanceVariable(
    ObjCContainerRecord *record, StringRef name, APILoc loc,
    const AvailabilityInfo &availability, APIAccess access,
    ObjCInstanceVariableRecord::AccessControl accessControl, APILinkage linkage,
    const Decl *decl) {
  name = copyString(name);
  auto *ivar = ObjCInstanceVariableRecord::create(
      allocator, name, linkage, loc, availability, access, accessControl, decl);
  record->ivars.push_back(ivar);
  return ivar;
}

APIRecord *API::addTypeDef(StringRef name, APILoc loc,
                           const AvailabilityInfo &availability,
                           APIAccess access, const Decl *decl) {
  name = copyString(name);
  auto result = typeDefs.insert({name, nullptr});
  if (result.second) {
    auto *record =
        APIRecord::create(allocator, name, APILinkage::Unknown, APIFlags::None,
                          loc, availability, access, decl);
    result.first->second = record;
  }
  return result.first->second;
}

const GlobalRecord *API::findGlobalVariable(StringRef name) const {
  auto it = globals.find(name);
  if (it != globals.end() && it->second->kind == GVKind::Variable)
    return it->second;
  return nullptr;
}

const GlobalRecord *API::findFunction(StringRef name) const {
  auto it = globals.find(name);
  if (it != globals.end() && it->second->kind == GVKind::Function)
    return it->second;
  return nullptr;
}

const APIRecord *API::findTypeDef(StringRef name) const {
  auto it = typeDefs.find(name);
  if (it != typeDefs.end())
    return it->second;
  return nullptr;
}

const EnumConstantRecord *API::findEnumConstant(StringRef name) const {
  auto it = enumConstants.find(name);
  if (it != enumConstants.end())
    return it->second;
  return nullptr;
}

const ObjCInterfaceRecord *API::findObjCInterface(StringRef name) const {
  auto it = interfaces.find(name);
  if (it != interfaces.end())
    return it->second;
  return nullptr;
}

const ObjCProtocolRecord *API::findObjCProtocol(StringRef name) const {
  auto it = protocols.find(name);
  if (it != protocols.end())
    return it->second;
  return nullptr;
}

const ObjCCategoryRecord *API::findObjCCategory(StringRef interfaceName,
                                                StringRef name) const {
  auto it = categories.find({interfaceName, name});
  if (it != categories.end())
    return it->second;
  return nullptr;
}

void API::visit(APIVisitor &visitor) const {
  for (auto &it : typeDefs)
    visitor.visitTypeDef(*it.second);
  for (auto &it : globals)
    visitor.visitGlobal(*it.second);
  for (auto &it : enumConstants)
    visitor.visitEnumConstant(*it.second);
  for (auto &it : protocols)
    visitor.visitObjCProtocol(*it.second);
  for (auto &it : interfaces)
    visitor.visitObjCInterface(*it.second);
  for (auto &it : categories)
    visitor.visitObjCCategory(*it.second);
}

void API::visit(APIMutator &visitor) {
  for (auto &it : typeDefs)
    visitor.visitTypeDef(*it.second);
  for (auto &it : globals)
    visitor.visitGlobal(*it.second);
  for (auto &it : enumConstants)
    visitor.visitEnumConstant(*it.second);
  for (auto &it : protocols)
    visitor.visitObjCProtocol(*it.second);
  for (auto &it : interfaces)
    visitor.visitObjCInterface(*it.second);
  for (auto &it : categories)
    visitor.visitObjCCategory(*it.second);
}

StringRef API::copyString(StringRef string) {
  if (string.empty())
    return {};

  void *ptr = allocator.Allocate(string.size(), 1);
  memcpy(ptr, string.data(), string.size());
  return StringRef(reinterpret_cast<const char *>(ptr), string.size());
}

BinaryInfo &API::getBinaryInfo() {
  if (hasBinaryInfo())
    return *binaryInfo;
  binaryInfo.emplace();
  return *binaryInfo;
}

TAPI_NAMESPACE_INTERNAL_END
