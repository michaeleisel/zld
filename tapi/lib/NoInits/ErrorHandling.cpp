//===- ErrorHandling.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>
#include <string>

namespace llvm {

void report_bad_alloc_error(const char *, bool) { abort(); }

void report_fatal_error(const char *, bool) { abort(); }

void report_fatal_error(const std::string &, bool) { abort(); }

void report_fatal_error(StringRef, bool) { abort(); }

void report_fatal_error(const Twine &, bool) { abort(); }

void llvm_unreachable_internal(const char *, const char *, unsigned) {
  abort();
}

} // end namespace llvm.
