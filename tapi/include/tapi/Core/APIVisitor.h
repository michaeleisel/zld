//===- tapi/Core/APIVisitor.h - TAPI API Visitor ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the API Visitor.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_APIVISITOR_H
#define TAPI_CORE_APIVISITOR_H

#include "tapi/Core/API.h"
#include "tapi/Defines.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

class APIVisitor {
public:
  virtual ~APIVisitor();

  virtual void visitGlobal(const GlobalRecord &);
  virtual void visitEnumConstant(const EnumConstantRecord &);
  virtual void visitObjCInterface(const ObjCInterfaceRecord &);
  virtual void visitObjCCategory(const ObjCCategoryRecord &);
  virtual void visitObjCProtocol(const ObjCProtocolRecord &);
  virtual void visitTypeDef(const APIRecord &);
};

class APIMutator {
public:
  virtual ~APIMutator();

  virtual void visitGlobal(GlobalRecord &);
  virtual void visitEnumConstant(EnumConstantRecord &);
  virtual void visitObjCInterface(ObjCInterfaceRecord &);
  virtual void visitObjCCategory(ObjCCategoryRecord &);
  virtual void visitObjCProtocol(ObjCProtocolRecord &);
  virtual void visitTypeDef(APIRecord &);
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_APIVISITOR_H
