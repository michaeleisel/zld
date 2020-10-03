//===- tapi/Core/FileSystem.h - File System ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Additional file system support that is missing in LLVM.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_FILE_SYSTEM_H
#define TAPI_CORE_FILE_SYSTEM_H

#include "tapi/Core/LLVM.h"
#include "tapi/Defines.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include <system_error>

TAPI_NAMESPACE_INTERNAL_BEGIN

std::error_code realpath(SmallVectorImpl<char> &path);

std::error_code read_link(const Twine &path, SmallVectorImpl<char> &linkPath);

std::error_code shouldSkipSymlink(const Twine &path, bool &result);

std::error_code make_relative(StringRef from, StringRef to,
                              SmallVectorImpl<char> &relativePath);

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_FILE_SYSTEM_H
