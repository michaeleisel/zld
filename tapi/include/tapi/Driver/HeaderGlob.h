//===--- tapi/Driver/HeaderGlob.h - Header Glob -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief A simple header file glob matcher.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_HEADERGLOB_H
#define TAPI_CORE_HEADERGLOB_H

#include "tapi/Core/HeaderFile.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Driver/Glob.h"
#include "tapi/Defines.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Regex.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

class HeaderGlob {
public:
  HeaderGlob(StringRef globString, llvm::Regex &&, HeaderType type);

  static llvm::Expected<std::unique_ptr<HeaderGlob>>
  create(StringRef globString, HeaderType type);
  bool match(const HeaderFile &header);
  bool didMatch() { return foundMatch; }
  StringRef str() { return globString; }

private:
  std::string globString;
  llvm::Regex regex;
  HeaderType headerType;
  bool foundMatch{false};
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_HEADERGLOB_H
