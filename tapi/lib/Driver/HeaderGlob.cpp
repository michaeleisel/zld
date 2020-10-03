//===- lib/Driver/HeaderGlob.cpp - Header Glob ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements a simple header file glob matcher.
///
//===----------------------------------------------------------------------===//

#include "tapi/Driver/HeaderGlob.h"
#include "llvm/ADT/SmallString.h"

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

HeaderGlob::HeaderGlob(StringRef globString, Regex &&regex, HeaderType type)
    : globString(globString), regex(std::move(regex)), headerType(type) {}

bool HeaderGlob::match(const HeaderFile &header) {
  if (header.type != headerType)
    return false;

  bool result = regex.match(header.fullPath);
  if (result)
    foundMatch = true;
  return result;
}

Expected<std::unique_ptr<HeaderGlob>> HeaderGlob::create(StringRef globString,
                                                         HeaderType type) {
  auto regex = createRegexFromGlob(globString);
  if (!regex)
    return regex.takeError();

  return make_unique<HeaderGlob>(globString, std::move(*regex), type);
}

TAPI_NAMESPACE_INTERNAL_END
