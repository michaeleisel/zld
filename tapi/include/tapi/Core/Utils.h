//===- tapi/Core/Utils.h - TAPI Utility Methods -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Misc utility methods.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_UTILS_H
#define TAPI_CORE_UTILS_H

#include "tapi/Core/LLVM.h"
#include "tapi/Defines.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

class FileManager;

bool isPublicLocation(StringRef path);
bool isHeaderFile(StringRef path);
std::string findLibrary(StringRef installName, FileManager &fm,
                        ArrayRef<std::string> frameworkSearchPaths,
                        ArrayRef<std::string> librarySearchPaths,
                        ArrayRef<std::string> searchPaths);

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_UTILS_H
