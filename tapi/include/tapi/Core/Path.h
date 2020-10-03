//===- tapi/Core/Path.h - Path Operating System Concept ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Modified path support to handle frameworks.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_PATH_H
#define TAPI_CORE_PATH_H

#include "tapi/Core/LLVM.h"
#include "tapi/Defines.h"
#include "llvm/Support/Error.h"

#include <functional>
#include <string>
#include <vector>

TAPI_NAMESPACE_INTERNAL_BEGIN

class FileManager;

using PathSeq = std::vector<std::string>;

void replace_extension(SmallVectorImpl<char> &path, const Twine &extension);

llvm::Expected<PathSeq>
enumerateFiles(FileManager &fm, StringRef path,
               const std::function<bool(StringRef)> &func);

llvm::Expected<PathSeq> enumerateHeaderFiles(FileManager &fm, StringRef path);

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_PATH_H
