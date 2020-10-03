//===- lib/Core/XPISet.cpp - TAPI XPI Set -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the XPI set
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/XPISet.h"
#include "tapi/Defines.h"
#include "clang/Basic/SourceLocation.h"

using namespace llvm;
using clang::PresumedLoc;

TAPI_NAMESPACE_INTERNAL_BEGIN

GlobalSymbol *XPISet::addGlobalSymbolImp(StringRef name, APILinkage linkage,
                                         APIFlags flags, APIAccess access) {
  name = copyString(name);
  GlobalSymbol *globalSymbol;
  auto result =
      _symbols.try_emplace(SymbolsMapKey{XPIKind::GlobalSymbol, name}, nullptr);
  if (result.second) {
    globalSymbol =
        GlobalSymbol::create(allocator, name, linkage, flags, access);
    result.first->second = globalSymbol;
  } else {
    globalSymbol = cast<GlobalSymbol>(result.first->second);
    assert(globalSymbol->getFlags() == flags && "flags are not equal");
  }

  auto success = globalSymbol->updateAccess(access);
  assert(success && "Access is not equal");
  (void)success;

  return globalSymbol;
}

ObjCClass *XPISet::addObjCClassImpl(StringRef name, APILinkage linkage,
                                    APIAccess access) {
  name = copyString(name);
  ObjCClass *objcClass = nullptr;
  auto result = _symbols.try_emplace(
      SymbolsMapKey{XPIKind::ObjectiveCClass, name}, nullptr);
  if (result.second) {
    objcClass = ObjCClass::create(allocator, name, linkage, access);
    result.first->second = objcClass;
  } else {
    objcClass = cast<ObjCClass>(result.first->second);
  }

  auto success = objcClass->updateAccess(access);
  assert(success && "Access is not equal");
  (void)success;

  return objcClass;
}

ObjCClassEHType *XPISet::addObjCClassEHTypeImpl(StringRef name,
                                                APILinkage linkage,
                                                APIAccess access) {
  name = copyString(name);
  ObjCClassEHType *objCClassEH = nullptr;
  auto result = _symbols.try_emplace(
      SymbolsMapKey{XPIKind::ObjectiveCClassEHType, name}, nullptr);
  if (result.second) {
    objCClassEH = ObjCClassEHType::create(allocator, name, linkage, access);
    result.first->second = objCClassEH;
  } else {
    objCClassEH = cast<ObjCClassEHType>(result.first->second);
  }

  auto success = objCClassEH->updateAccess(access);
  assert(success && "Access is not equal");
  (void)success;

  return objCClassEH;
}

ObjCInstanceVariable *XPISet::addObjCInstanceVariableImpl(StringRef name,
                                                          APILinkage linkage,
                                                          APIAccess access) {
  name = copyString(name);
  ObjCInstanceVariable *objcInstanceVariable = nullptr;
  auto result = _symbols.try_emplace(
      SymbolsMapKey{XPIKind::ObjectiveCInstanceVariable, name}, nullptr);

  if (result.second) {
    objcInstanceVariable =
        ObjCInstanceVariable::create(allocator, name, linkage, access);
    result.first->second = objcInstanceVariable;
  } else {
    objcInstanceVariable = cast<ObjCInstanceVariable>(result.first->second);
  }

  auto success = objcInstanceVariable->updateAccess(access);
  assert(success && "Access is not equal");
  (void)success;

  return objcInstanceVariable;
}

const XPI *XPISet::findSymbol(XPIKind kind, StringRef name) const {
  auto it = _symbols.find({kind, name});
  if (it != _symbols.end())
    return it->second;
  return nullptr;
}

const XPI *XPISet::findSymbol(const XPI &xpi) const {
  return findSymbol(xpi.getKind(), xpi.getName());
}

bool XPISet::removeSymbol(XPIKind kind, StringRef name) {
  auto it = _symbols.find({kind, name});
  if (it == _symbols.end())
    return false;

  _symbols.erase(it);
  return true;
}

XPISet::const_symbol_range XPISet::symbols() const {
  return {_symbols.begin(), _symbols.end()};
}

XPISet::const_filtered_symbol_range XPISet::exports() const {
  std::function<bool(const XPI *)> fn = [](const XPI *xpi) {
    return xpi->isAvailable() && !xpi->isUndefined();
  };
  return make_filter_range(
      make_range<const_symbol_iterator>({_symbols.begin()}, {_symbols.end()}),
      fn);
}

XPISet::const_filtered_symbol_range XPISet::undefineds() const {
  std::function<bool(const XPI *)> fn = [](const XPI *xpi) {
    return xpi->isAvailable() && xpi->isUndefined();
  };
  return make_filter_range(
      make_range<const_symbol_iterator>({_symbols.begin()}, {_symbols.end()}),
      fn);
}

TAPI_NAMESPACE_INTERNAL_END
