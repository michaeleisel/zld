//===- lib/Core/XPI.cpp - TAPI XPI ------------------------------*- C++ -*-===//
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

#include "tapi/Core/XPI.h"
#include "tapi/Defines.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

XPI::const_target_range XPI::targets() const {
  return make_range<const_target_iterator>(_availability.begin(),
                                           _availability.end());
}

XPI::const_filtered_target_range
XPI::targets(ArchitectureSet architectures) const {
  std::function<bool(const Target &)> fn =
      [architectures](const Target &target) {
        return architectures.has(target.architecture);
      };
  return make_filter_range(make_range<const_target_iterator>(
                               _availability.begin(), _availability.end()),
                           fn);
}

std::string XPI::getPrettyName(bool demangle) const {
  if (!demangle)
    return _name;

  if (demangle && _name.startswith("__Z")) {
    int status = 0;
    char *demangledName = itaniumDemangle(_name.substr(1).str().c_str(),
                                          nullptr, nullptr, &status);
    if (status == 0) {
      std::string result = demangledName;
      free(demangledName);
      return result;
    }
  }

  if (_name[0] == '_')
    return _name.substr(1);

  return _name;
}

std::string XPI::getAnnotatedName(bool demangle) const {
  std::string name;
  if (isWeakDefined())
    name += "(weak-def) ";
  if (isWeakReferenced())
    name += "(weak-ref) ";
  if (isThreadLocalValue())
    name += "(tlv) ";
  switch (getKind()) {
  case XPIKind::GlobalSymbol:
    return name + getPrettyName(demangle);
  case XPIKind::ObjectiveCClass:
    return name + "(ObjC Class) " + _name.str();
  case XPIKind::ObjectiveCClassEHType:
    return name + "(ObjC Class EH) " + _name.str();
  case XPIKind::ObjectiveCInstanceVariable:
    return name + "(ObjC IVar) " + _name.str();
  }
}

void XPI::print(raw_ostream &os) const {
  switch (getAccess()) {
  case APIAccess::Unknown:
    os << "(unknown) ";
    break;
  case APIAccess::Public:
    os << "(public) ";
    break;
  case APIAccess::Private:
    os << "(private) ";
    break;
  case APIAccess::Project:
    os << "(project) ";
    break;
  }
  os << getAnnotatedName();
  for (const auto &avail : _availability)
    os << " [" << avail.first << ": " << avail.second << "]";
}

GlobalSymbol *GlobalSymbol::create(BumpPtrAllocator &A, StringRef name,
                                   APILinkage linkage, APIFlags flags,
                                   APIAccess access) {
  return new (A) GlobalSymbol(name, linkage, flags, access);
}

ObjCInstanceVariable *ObjCInstanceVariable::create(BumpPtrAllocator &A,
                                                   StringRef name,
                                                   APILinkage linkage,
                                                   APIAccess access) {
  return new (A) ObjCInstanceVariable(name, linkage, access);
}

ObjCClass *ObjCClass::create(BumpPtrAllocator &A, StringRef name,
                             APILinkage linkage, APIAccess access) {
  return new (A) ObjCClass(name, linkage, access);
}

ObjCClassEHType *ObjCClassEHType::create(BumpPtrAllocator &A, StringRef name,
                                         APILinkage linkage, APIAccess access) {
  return new (A) ObjCClassEHType(name, linkage, access);
}

TAPI_NAMESPACE_INTERNAL_END
