//===- tapi/Core/Framework.h - TAPI Framework -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the content of a framework, such as public and private
/// header files, and dynamic libraries.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_FRAMEWORK_H
#define TAPI_CORE_FRAMEWORK_H

#include "tapi/Core/HeaderFile.h"
#include "tapi/Core/InterfaceFile.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Core/Path.h"
#include "tapi/Core/XPI.h"
#include "tapi/Defines.h"
#include "tapi/Frontend/FrontendContext.h"
#include "llvm/ADT/StringRef.h"
#include <map>
#include <string>
#include <vector>

TAPI_NAMESPACE_INTERNAL_BEGIN

enum class HeaderType;

struct Framework {
  std::string _baseDirectory;
  HeaderSeq _headerFiles;
  PathSeq _moduleMaps;
  PathSeq _dynamicLibraryFiles;
  std::vector<Framework> _subFrameworks;
  std::vector<Framework> _versions;
  std::vector<std::unique_ptr<InterfaceFile>> _interfaceFiles;
  std::unique_ptr<XPISet> _headerSymbols;
  std::vector<FrontendContext> _frontendResults;
  bool isDynamicLibrary{false};
  bool isSysRoot{false};

  Framework(StringRef directory) : _baseDirectory(directory) {}

  StringRef getName() const;

  StringRef getPath() const { return _baseDirectory; }

  void addHeaderFile(StringRef fullPath, HeaderType type,
                     StringRef relativePath = StringRef()) {
    _headerFiles.emplace_back(fullPath, type, relativePath);
  }

  void addModuleMap(StringRef path) { _moduleMaps.emplace_back(path); }

  void addDynamicLibraryFile(StringRef path) {
    _dynamicLibraryFiles.emplace_back(path);
  }
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_FRAMEWORK_H
