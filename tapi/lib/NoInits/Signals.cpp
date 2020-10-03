//===- Signals.cpp ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include <cstdlib>
#include <string>

namespace llvm {

namespace sys {

bool RemoveFileOnSignal(StringRef Filename, std::string *ErrMsg) { abort(); }

void DontRemoveFileOnSignal(StringRef Filename) { abort(); }

void AddSignalHandler(void (*)(void *), void *) { abort(); }

} // end namespace sys.
} // end namespace llvm.
