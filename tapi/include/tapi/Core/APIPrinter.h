//===- tapi/Core/APIPrinter.h - TAPI API Printer ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Declares the TAPI API Printer
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_APIPRINTER_H
#define TAPI_CORE_APIPRINTER_H

#include "tapi/Core/APIVisitor.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

class APIPrinter : public APIVisitor {
public:
  APIPrinter(raw_ostream &os, bool useColor = true);
  ~APIPrinter() override;

  void visitGlobal(const GlobalRecord &) override;
  void visitEnumConstant(const EnumConstantRecord &) override;
  void visitObjCInterface(const ObjCInterfaceRecord &) override;
  void visitObjCCategory(const ObjCCategoryRecord &) override;
  void visitObjCProtocol(const ObjCProtocolRecord &) override;
  void visitTypeDef(const APIRecord &) override;

private:
  void printMethod(const ObjCMethodRecord *method);
  void printProperty(const ObjCPropertyRecord *property);
  void printInstanceVariable(const ObjCInstanceVariableRecord *ivar);

  raw_ostream &os;
  bool hasColors = false;

  bool emittedHeaderGlobal = false;
  bool emittedHeaderEnum = false;
  bool emittedHeaderInterface = false;
  bool emittedHeaderCategory = false;
  bool emittedHeaderProtocol = false;
  bool emittedHeaderTypedef = false;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_APIPRINTER_H
