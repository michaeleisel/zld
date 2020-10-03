//===- InterfaceFileManager.cpp - TAPI Interface File Manager ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the TAPI Interface File Manager.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/InterfaceFileManager.h"
#include "tapi/Core/FileManager.h"
#include "tapi/Core/Registry.h"
#include "tapi/Defines.h"
#include "llvm/Support/Error.h"

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

InterfaceFileManager::InterfaceFileManager(FileManager &fm) : _fm(fm) {
  _registry.addYAMLReaders();
  _registry.addYAMLWriters();
  _registry.addBinaryReaders();
}

Expected<InterfaceFile *>
InterfaceFileManager::readFile(const std::string &path) {
  auto *file = _fm.getFile(path);
  if (file == nullptr)
    return errorCodeToError(
        std::make_error_code(std::errc::no_such_file_or_directory));

  auto bufferOrErr = _fm.getBufferForFile(file);
  if (!bufferOrErr)
    return errorCodeToError(bufferOrErr.getError());

  auto interface =
      _registry.readFile(std::move(bufferOrErr.get()), ReadFlags::Symbols);
  if (!interface)
    return interface.takeError();

  auto it = _libraries.find(interface.get()->getInstallName());
  if (it != _libraries.end())
    return it->second.get();

  auto result = _libraries.emplace(interface.get()->getInstallName(),
                                   std::move(interface.get()));
  return result.first->second.get();
}

Error InterfaceFileManager::writeFile(const std::string &path,
                                      const InterfaceFile *file,
                                      VersionedFileType fileType) const {
  return _registry.writeFile(path, file, fileType);
}

TAPI_NAMESPACE_INTERNAL_END
