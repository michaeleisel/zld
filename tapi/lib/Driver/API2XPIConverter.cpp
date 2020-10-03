//===- lib/Driver/API2XPIConverter.cpp - API2XPI Converter ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements API2XPI Converter
///
//===----------------------------------------------------------------------===//

#include "API2XPIConverter.h"
#include "clang/AST/DeclObjC.h"

using namespace llvm;
using namespace clang;

TAPI_NAMESPACE_INTERNAL_BEGIN

void API2XPIConverter::visitGlobal(const GlobalRecord &record) {
  // Skip non exported symbol.
  if (!record.isExported())
    return;

  if (record.kind == GVKind::Variable) {
    auto nameRef = record.name;
    if (nameRef.consume_front(".objc_class_name_") ||
        nameRef.consume_front("_OBJC_CLASS_$_") ||
        nameRef.consume_front("_OBJC_METACLASS_$_"))
      xpiSet->addObjCClass(nameRef, APILinkage::Exported, target, record.access,
                           record.availability);
    else if (nameRef.consume_front("_OBJC_EHTYPE_$_"))
      xpiSet->addObjCClassEHType(nameRef, APILinkage::Exported, target,
                                 record.access, record.availability);
    else if (nameRef.consume_front("_OBJC_IVAR_$_"))
      xpiSet->addObjCInstanceVariable(nameRef, APILinkage::Exported, target,
                                      record.access, record.availability);
    else
      xpiSet->addGlobalSymbol(record.name, APILinkage::Exported,
                              record.isWeakDefined() ? APIFlags::WeakDefined
                                                     : APIFlags::None,
                              target, record.access, record.availability);
  } else if (record.kind == GVKind::Function) {
    xpiSet->addGlobalSymbol(record.name, APILinkage::Exported,
                            record.isWeakDefined() ? APIFlags::WeakDefined
                                                   : APIFlags::None,
                            target, record.access, record.availability);
  }
}

void API2XPIConverter::visitObjCInterface(const ObjCInterfaceRecord &record) {
  if (!record.isExported())
    return;

  xpiSet->addObjCClass(record.name, APILinkage::Exported, target, record.access,
                       record.availability);
  if (record.hasExceptionAttribute)
    xpiSet->addObjCClassEHType(record.name, APILinkage::Exported, target,
                               record.access, record.availability);

  auto addIvars = [&](ArrayRef<const ObjCInstanceVariableRecord *> ivars) {
    for (const auto *ivar : ivars) {
      if (!ivar->isExported())
        continue;
      // ObjC has an additional mechanism to specify if an ivar is exported or
      // not.
      if (ivar->accessControl == ObjCIvarDecl::Private ||
          ivar->accessControl == ObjCIvarDecl::Package)
        continue;
      std::string name = (record.name + "." + ivar->name).str();
      xpiSet->addObjCInstanceVariable(name, APILinkage::Exported, target,
                                      ivar->access, ivar->availability);
    }
  };
  addIvars(record.ivars);

  for (const auto *category : record.categories)
    addIvars(category->ivars);
}

void API2XPIConverter::visitObjCCategory(const ObjCCategoryRecord &record) {
  auto addIvars = [&](ArrayRef<const ObjCInstanceVariableRecord *> ivars) {
    for (const auto *ivar : ivars) {
      if (!ivar->isExported())
        continue;
      // ObjC has an additional mechanism to specify if an ivar is exported or
      // not.
      if (ivar->accessControl == ObjCIvarDecl::Private ||
          ivar->accessControl == ObjCIvarDecl::Package)
        continue;
      std::string name = (record.interfaceName + "." + ivar->name).str();
      xpiSet->addObjCInstanceVariable(name, APILinkage::Exported, target,
                                      ivar->access, ivar->availability);
    }
  };
  addIvars(record.ivars);
}

TAPI_NAMESPACE_INTERNAL_END
