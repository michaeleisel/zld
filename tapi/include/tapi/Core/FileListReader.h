//===- tapi/Core/FileListReader.h - File List Reader ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief The JSON File List is used to communicate additional information to
///        InstallAPI. For now this only includes a header list.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_FILE_LIST_READER_H
#define TAPI_CORE_FILE_LIST_READER_H

#include "tapi/Core/HeaderFile.h"
#include "tapi/Defines.h"
#include "llvm/Support/Error.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

class FileListReader {
  class Implementation;

  Implementation &impl;

  FileListReader(std::unique_ptr<MemoryBuffer> inputBuffer, llvm::Error &error);

public:
  static llvm::Expected<std::unique_ptr<FileListReader>>
  get(std::unique_ptr<llvm::MemoryBuffer> inputBuffer);

  ~FileListReader();

  FileListReader(const FileListReader &) = delete;
  FileListReader &operator=(const FileListReader &) = delete;

  int getVersion() const;

  /// Visitor used when walking the contents of the file list.
  class Visitor {
  public:
    virtual ~Visitor();

    virtual void visitHeaderFile(HeaderType type, StringRef path);
  };

  /// Visit the contents of the header list file, passing each entity to the
  /// given visitor.
  void visit(Visitor &visitor);
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_FILE_LIST_READER_H
