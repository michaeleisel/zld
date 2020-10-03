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

#include "tapi/Core/HeaderFile.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

void HeaderFile::print(raw_ostream &os) const {
  switch (type) {
  case HeaderType::Public:
    os << "(public) ";
    break;
  case HeaderType::Private:
    os << "(private) ";
    break;
  case HeaderType::Project:
    os << "(project) ";
    break;
  }
  os << sys::path::filename(fullPath);
  if (isUmbrellaHeader)
    os << " (umbrella header)";
  if (isExtra)
    os << " (extra header)";
  if (isExcluded)
    os << " (excluded header)";
  if (isPreInclude)
    os << " (pre-include header)";
}

TAPI_NAMESPACE_INTERNAL_END
