//===- lib/Core/Framework.cpp - Framework Context ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the Framework context.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/Framework.h"
#include "tapi/Core/HeaderFile.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <set>

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

StringRef Framework::getName() const {
  StringRef path = _baseDirectory;
  // Returns the framework name extract from path.
  while (!path.empty()) {
    if (path.endswith(".framework"))
      return sys::path::filename(path);
    path = sys::path::parent_path(path);
  }

  // Otherwise, return the name of the baseDirectory.
  // First, remove all the trailing seperator.
  path = _baseDirectory;
  return sys::path::filename(path.rtrim("/"));
}

TAPI_NAMESPACE_INTERNAL_END
