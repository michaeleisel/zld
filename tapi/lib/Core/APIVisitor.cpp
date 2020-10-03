//===- lib/Core/APIMutator.cpp - TAPI API Visitor ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "tapi/Core/APIVisitor.h"

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

APIMutator::~APIMutator() {}

void APIMutator::visitGlobal(GlobalRecord &) {}
void APIMutator::visitEnumConstant(EnumConstantRecord &) {}
void APIMutator::visitObjCInterface(ObjCInterfaceRecord &) {}
void APIMutator::visitObjCCategory(ObjCCategoryRecord &) {}
void APIMutator::visitObjCProtocol(ObjCProtocolRecord &) {}
void APIMutator::visitTypeDef(APIRecord &) {}

APIVisitor::~APIVisitor() {}

void APIVisitor::visitGlobal(const GlobalRecord &) {}
void APIVisitor::visitEnumConstant(const EnumConstantRecord &) {}
void APIVisitor::visitObjCInterface(const ObjCInterfaceRecord &) {}
void APIVisitor::visitObjCCategory(const ObjCCategoryRecord &) {}
void APIVisitor::visitObjCProtocol(const ObjCProtocolRecord &) {}
void APIVisitor::visitTypeDef(const APIRecord &) {}

TAPI_NAMESPACE_INTERNAL_END
