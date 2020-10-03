//===--- tapi/Driver/DriverUtils.h - TAPI Driver Utilities ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef TAPI_DRIVER_DRIVER_UTILS_H
#define TAPI_DRIVER_DRIVER_UTILS_H

#include "tapi/Core/HeaderFile.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Core/Path.h"
#include "tapi/Defines.h"
#include <vector>

namespace clang {
class FileEntry;
}

TAPI_NAMESPACE_INTERNAL_BEGIN

class FileManager;
class DiagnosticsEngine;

bool findAndAddHeaderFiles(HeaderSeq &headersOut, FileManager &fm,
                           DiagnosticsEngine &diag, PathSeq headersIn,
                           HeaderType type, StringRef sysroot,
                           StringRef basePath);

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_DRIVER_DRIVER_UTILS_H
