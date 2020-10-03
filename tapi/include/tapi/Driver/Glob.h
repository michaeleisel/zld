//===--- tapi/Driver/Glob.h - Glob ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief A simple glob to regex converter.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_GLOB_H
#define TAPI_CORE_GLOB_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Regex.h"
#include "tapi/Defines.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

llvm::Expected<llvm::Regex> createRegexFromGlob(llvm::StringRef glob);

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_GLOB_H
