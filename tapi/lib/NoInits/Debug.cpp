//===- Debug.cpp ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/raw_ostream.h"
#include <cstdlib>

namespace llvm {

// Prevent the inclusion of lib/Support/Debug.cpp, because we don't want the
// static initializers.
raw_ostream &dbgs() { return errs(); }

#ifndef NDEBUG
#undef isCurrentDebugType
bool isCurrentDebugType(const char *Type) { abort(); }

bool DebugFlag = false;
#endif

} // end namespace llvm.
