//===- tapi/Core/API.h - TAPI API -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the API.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_API_H
#define TAPI_CORE_API_H

#include "tapi/Core/APICommon.h"
#include "tapi/Core/AvailabilityInfo.h"
#include "tapi/Core/InterfaceFile.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Defines.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Error.h"

using clang::Decl;

TAPI_NAMESPACE_INTERNAL_BEGIN

class APILoc {
public:
  APILoc() = default;
  APILoc(clang::PresumedLoc loc) : loc(loc) {}
  APILoc(StringRef file, unsigned line, unsigned col);

  bool isInvalid() const;
  StringRef getFilename() const;
  unsigned getLine() const;
  unsigned getColumn() const;
  clang::PresumedLoc getPresumedLoc() const;

private:
  llvm::Optional<clang::PresumedLoc> loc;
  StringRef file;
  unsigned line;
  unsigned col;
};

struct APIRecord {
  StringRef name;
  APILoc loc;
  const Decl *decl;
  AvailabilityInfo availability;
  APILinkage linkage;
  APIFlags flags;
  APIAccess access;

  static APIRecord *create(llvm::BumpPtrAllocator &allocator, StringRef name,
                           APILinkage linkage, APIFlags flags, APILoc loc,
                           const AvailabilityInfo &availability,
                           APIAccess access, const Decl *decl);

  bool isWeakDefined() const {
    return (flags & APIFlags::WeakDefined) == APIFlags::WeakDefined;
  }

  bool isWeakReferenced() const {
    return (flags & APIFlags::WeakReferenced) == APIFlags::WeakReferenced;
  }

  bool isThreadLocalValue() const {
    return (flags & APIFlags::ThreadLocalValue) == APIFlags::ThreadLocalValue;
  }

  bool isExported() const { return linkage >= APILinkage::Reexported; }
  bool isReexported() const { return linkage == APILinkage::Reexported; }
};

struct EnumConstantRecord : APIRecord {
  EnumConstantRecord(StringRef name, APILoc loc,
                     const AvailabilityInfo &availability, APIAccess access,
                     const Decl *decl)
      : APIRecord({name, loc, decl, availability, APILinkage::Unknown,
                   APIFlags::None, access}) {}
  static EnumConstantRecord *create(llvm::BumpPtrAllocator &allocator,
                                    StringRef name, APILoc loc,
                                    const AvailabilityInfo &availability,
                                    APIAccess access, const Decl *decl);
};

enum class GVKind {
  Unknown,
  Variable,
  Function,
};

struct GlobalRecord : APIRecord {
  GVKind kind;

  GlobalRecord(StringRef name, APIFlags flags, APILoc loc,
               const AvailabilityInfo &availability, APIAccess access,
               const Decl *decl, GVKind kind, APILinkage linkage)
      : APIRecord({name, loc, decl, availability, linkage, flags, access}),
        kind(kind) {}

  static GlobalRecord *create(llvm::BumpPtrAllocator &allocator, StringRef name,
                              APILinkage linkage, APIFlags flags, APILoc loc,
                              const AvailabilityInfo &availability,
                              APIAccess access, const Decl *decl, GVKind kind);
};

struct ObjCPropertyRecord : APIRecord {
  enum AttributeKind : unsigned {
    NoAttr = 0,
    ReadOnly = 1,
    Class = 1 << 1,
    Dynamic = 1 << 2,
  };

  AttributeKind attributes;
  StringRef getterName;
  StringRef setterName;
  bool isOptional;

  ObjCPropertyRecord(StringRef name, StringRef getterName, StringRef setterName,
                     APILoc loc, const AvailabilityInfo &availability,
                     APIAccess access, AttributeKind attributes,
                     bool isOptional, const Decl *decl)
      : APIRecord({name, loc, decl, availability, APILinkage::Unknown,
                   APIFlags::None, access}),
        attributes(attributes), getterName(getterName), setterName(setterName),
        isOptional(isOptional) {}

  static ObjCPropertyRecord *create(llvm::BumpPtrAllocator &allocator,
                                    StringRef name, StringRef getterName,
                                    StringRef setterName, APILoc loc,
                                    const AvailabilityInfo &availability,
                                    APIAccess access, AttributeKind attributes,
                                    bool isOptional, const Decl *decl);

  bool isReadOnly() const { return attributes & ReadOnly; }
  bool isDynamic() const { return attributes & Dynamic; }
  bool isClassProperty() const { return attributes & Class; }
};

struct ObjCInstanceVariableRecord : APIRecord {
  using AccessControl = clang::ObjCIvarDecl::AccessControl;
  AccessControl accessControl;

  ObjCInstanceVariableRecord(StringRef name, APILinkage linkage, APILoc loc,
                             const AvailabilityInfo &availability,
                             APIAccess access, AccessControl accessControl,
                             const Decl *decl)
      : APIRecord(
            {name, loc, decl, availability, linkage, APIFlags::None, access}),
        accessControl(accessControl) {}

  static ObjCInstanceVariableRecord *
  create(llvm::BumpPtrAllocator &allocator, StringRef name, APILinkage linkage,
         APILoc loc, const AvailabilityInfo &availability, APIAccess access,
         AccessControl accessControl, const Decl *decl);
};

struct ObjCMethodRecord : APIRecord {
  bool isInstanceMethod;
  bool isOptional;
  bool isDynamic;

  ObjCMethodRecord(StringRef name, APILoc loc,
                   const AvailabilityInfo &availability, APIAccess access,
                   bool isInstanceMethod, bool isOptional, bool isDynamic,
                   const Decl *decl)
      : APIRecord({name, loc, decl, availability, APILinkage::Unknown,
                   APIFlags::None, access}),
        isInstanceMethod(isInstanceMethod), isOptional(isOptional),
        isDynamic(isDynamic) {}

  static ObjCMethodRecord *create(llvm::BumpPtrAllocator &allocator,
                                  StringRef name, APILoc loc,
                                  const AvailabilityInfo &availability,
                                  APIAccess access, bool isInstanceMethod,
                                  bool isOptional, bool isDynamic,
                                  const Decl *decl);
};

struct ObjCContainerRecord : APIRecord {
  std::vector<ObjCMethodRecord *> methods;
  std::vector<ObjCPropertyRecord *> properties;
  std::vector<ObjCInstanceVariableRecord *> ivars;
  std::vector<StringRef> protocols;

  ObjCContainerRecord(StringRef name, APILinkage linkage, APILoc loc,
                      const AvailabilityInfo &availability, APIAccess access,
                      const Decl *decl)
      : APIRecord(
            {name, loc, decl, availability, linkage, APIFlags::None, access}) {}
};

struct ObjCCategoryRecord : ObjCContainerRecord {
  StringRef interfaceName;

  ObjCCategoryRecord(StringRef interfaceName, StringRef name, APILoc loc,
                     const AvailabilityInfo &availability, APIAccess access,
                     const Decl *decl)
      : ObjCContainerRecord(name, APILinkage::Unknown, loc, availability,
                            access, decl),
        interfaceName(interfaceName) {}

  static ObjCCategoryRecord *create(llvm::BumpPtrAllocator &allocator,
                                    StringRef interfaceName, StringRef name,
                                    APILoc loc,
                                    const AvailabilityInfo &availability,
                                    APIAccess access, const Decl *decl);
};

struct ObjCProtocolRecord : ObjCContainerRecord {
  ObjCProtocolRecord(StringRef name, APILoc loc,
                     const AvailabilityInfo &availability, APIAccess access,
                     const Decl *decl)
      : ObjCContainerRecord(name, APILinkage::Unknown, loc, availability,
                            access, decl) {}

  static ObjCProtocolRecord *create(llvm::BumpPtrAllocator &allocator,
                                    StringRef name, APILoc loc,
                                    const AvailabilityInfo &availability,
                                    APIAccess access, const Decl *decl);
};

struct ObjCInterfaceRecord : ObjCContainerRecord {
  std::vector<const ObjCCategoryRecord *> categories;
  StringRef superClassName;
  bool hasExceptionAttribute = false;

  ObjCInterfaceRecord(StringRef name, APILinkage linkage, APILoc loc,
                      const AvailabilityInfo &availability, APIAccess access,
                      StringRef superClassName, const Decl *decl)
      : ObjCContainerRecord(name, linkage, loc, availability, access, decl),
        superClassName(superClassName) {}

  static ObjCInterfaceRecord *
  create(llvm::BumpPtrAllocator &allocator, StringRef name, APILinkage linkage,
         APILoc loc, const AvailabilityInfo &availability, APIAccess access,
         StringRef superClassName, const Decl *decl);
};

class APIVisitor;
class APIMutator;

struct BinaryInfo {
  FileType fileType = FileType::Invalid;
  PackedVersion currentVersion;
  PackedVersion compatibilityVersion;
  uint8_t swiftABIVersion = 0;
  bool isTwoLevelNamespace = false;
  bool isAppExtensionSafe = false;
  StringRef parentUmbrella;
  std::vector<StringRef> allowableClients;
  std::vector<StringRef> reexportedLibraries;
  StringRef installName;
  StringRef uuid;
};

class API {
public:
  API(const llvm::Triple &triple) : target(triple) {}
  const llvm::Triple &getTarget() const { return target; }

  static bool updateAPIAccess(APIRecord *record, APIAccess access);
  static bool updateAPILinkage(APIRecord *record, APILinkage linkage);

  EnumConstantRecord *addEnumConstant(StringRef name, APILoc loc,
                                      const AvailabilityInfo &availability,
                                      APIAccess access, const Decl *decl);
  GlobalRecord *
  addGlobal(StringRef name, APILoc loc, const AvailabilityInfo &availability,
            APIAccess access, const Decl *decl, GVKind kind = GVKind::Unknown,
            APILinkage linkage = APILinkage::Unknown,
            bool isWeakDefined = false, bool isThreadLocal = false);
  GlobalRecord *addGlobal(StringRef name, APIFlags flags, APILoc loc,
                          const AvailabilityInfo &availability,
                          APIAccess access, const Decl *decl,
                          GVKind kind = GVKind::Unknown,
                          APILinkage linkage = APILinkage::Unknown);
  GlobalRecord *addGlobalVariable(StringRef name, APILoc loc,
                                  const AvailabilityInfo &availability,
                                  APIAccess access, const Decl *decl,
                                  APILinkage linkage = APILinkage::Unknown,
                                  bool isWeakDefined = false,
                                  bool isThreadLocal = false);
  GlobalRecord *addFunction(StringRef name, APILoc loc,
                            const AvailabilityInfo &availability,
                            APIAccess access, const Decl *decl,
                            APILinkage linkage = APILinkage::Unknown,
                            bool isWeakDefined = false);
  ObjCInterfaceRecord *addObjCInterface(StringRef name, APILoc loc,
                                        const AvailabilityInfo &availability,
                                        APIAccess access, APILinkage linkage,
                                        StringRef superClassName,
                                        const Decl *decl);
  ObjCCategoryRecord *addObjCCategory(StringRef interfaceName, StringRef name,
                                      APILoc loc,
                                      const AvailabilityInfo &availability,
                                      APIAccess access, const Decl *decl);
  ObjCProtocolRecord *addObjCProtocol(StringRef name, APILoc loc,
                                      const AvailabilityInfo &availability,
                                      APIAccess access, const Decl *decl);
  ObjCMethodRecord *addObjCMethod(ObjCContainerRecord *record, StringRef name,
                                  APILoc loc,
                                  const AvailabilityInfo &availability,
                                  APIAccess access, bool isInstanceMethod,
                                  bool isOptional, bool isDynamic,
                                  const Decl *decl);
  ObjCPropertyRecord *
  addObjCProperty(ObjCContainerRecord *record, StringRef name,
                  StringRef getterName, StringRef setterName, APILoc loc,
                  const AvailabilityInfo &availability, APIAccess access,
                  ObjCPropertyRecord::AttributeKind attributes, bool isOptional,
                  const Decl *decl);
  ObjCInstanceVariableRecord *addObjCInstanceVariable(
      ObjCContainerRecord *record, StringRef name, APILoc loc,
      const AvailabilityInfo &availability, APIAccess access,
      ObjCInstanceVariableRecord::AccessControl accessControl,
      APILinkage linkage, const Decl *decl);

  APIRecord *addTypeDef(StringRef name, APILoc loc,
                        const AvailabilityInfo &availability, APIAccess access,
                        const Decl *decl);

  void addPotentiallyDefinedSelector(StringRef name) {
    potentiallyDefinedSelectors.insert(name);
  }

  llvm::StringSet<> &getPotentiallyDefinedSelectors() {
    return potentiallyDefinedSelectors;
  }

  const llvm::StringSet<> &getPotentiallyDefinedSelectors() const {
    return potentiallyDefinedSelectors;
  }

  void visit(APIMutator &visitor);
  void visit(APIVisitor &visitor) const;

  const GlobalRecord *findGlobalVariable(StringRef) const;
  const GlobalRecord *findFunction(StringRef) const;
  const APIRecord *findTypeDef(StringRef) const;
  const EnumConstantRecord *findEnumConstant(StringRef name) const;
  const ObjCInterfaceRecord *findObjCInterface(StringRef) const;
  const ObjCProtocolRecord *findObjCProtocol(StringRef) const;
  const ObjCCategoryRecord *findObjCCategory(StringRef, StringRef) const;

  bool hasBinaryInfo() const { return binaryInfo.hasValue(); }
  BinaryInfo &getBinaryInfo();
  const BinaryInfo &getBinaryInfo() const {
    assert(hasBinaryInfo() && "must have binary info");
    return *binaryInfo;
  }

  StringRef copyString(StringRef string);

private:
  using APIRecordMap = llvm::MapVector<StringRef, APIRecord *>;
  using GlobalRecordMap = llvm::MapVector<StringRef, GlobalRecord *>;
  using EnumConstantRecordMap =
      llvm::MapVector<StringRef, EnumConstantRecord *>;
  using ObjCInterfaceRecordMap =
      llvm::MapVector<StringRef, ObjCInterfaceRecord *>;
  using ObjCCategoryRecordMap =
      llvm::MapVector<std::pair<StringRef, StringRef>, ObjCCategoryRecord *>;
  using ObjCProtocolRecordMap =
      llvm::MapVector<StringRef, ObjCProtocolRecord *>;

  llvm::BumpPtrAllocator allocator;

  const llvm::Triple target;

  GlobalRecordMap globals;
  EnumConstantRecordMap enumConstants;
  ObjCInterfaceRecordMap interfaces;
  ObjCCategoryRecordMap categories;
  ObjCProtocolRecordMap protocols;
  APIRecordMap typeDefs;
  llvm::StringSet<> potentiallyDefinedSelectors;

  llvm::Optional<BinaryInfo> binaryInfo;

  friend class APIVerifier;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_API_H
