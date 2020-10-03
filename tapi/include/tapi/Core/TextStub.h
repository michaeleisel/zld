//===- tapi/Core/TextStub_v1.h - Text Stub v1 -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the content of a text stub file.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_TEXT_STUB_H
#define TAPI_CORE_TEXT_STUB_H

#include "tapi/Core/LLVM.h"
#include "tapi/Core/Registry.h"
#include "tapi/Core/YAMLReaderWriter.h"
#include "tapi/Defines.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {
namespace yaml {
class IO;
} // namespace yaml
} // namespace llvm

TAPI_NAMESPACE_INTERNAL_BEGIN

namespace stub {
namespace v1 {

class YAMLDocumentHandler : public DocumentHandler {
  bool canRead(MemoryBufferRef memBufferRef,
               FileType types = FileType::All) const override;
  FileType getFileType(MemoryBufferRef memBufferRef) const override;
  bool canWrite(const InterfaceFile *file,
                VersionedFileType fileType) const override;
  bool handleDocument(llvm::yaml::IO &io,
                      const InterfaceFile *&file) const override;
};

} // end namespace v1.
} // end namespace stub.

namespace stub {
namespace v2 {

class YAMLDocumentHandler : public DocumentHandler {
  bool canRead(MemoryBufferRef memBufferRef,
               FileType types = FileType::All) const override;
  FileType getFileType(MemoryBufferRef memBufferRef) const override;
  bool canWrite(const InterfaceFile *file,
                VersionedFileType fileType) const override;
  bool handleDocument(llvm::yaml::IO &io,
                      const InterfaceFile *&file) const override;
};

} // end namespace v2.
} // end namespace stub.

namespace stub {
namespace v3 {

class YAMLDocumentHandler : public DocumentHandler {
  bool canRead(MemoryBufferRef memBufferRef,
               FileType types = FileType::All) const override;
  FileType getFileType(MemoryBufferRef memBufferRef) const override;
  bool canWrite(const InterfaceFile *file,
                VersionedFileType fileType) const override;
  bool handleDocument(llvm::yaml::IO &io,
                      const InterfaceFile *&file) const override;
};

} // end namespace v3.
} // end namespace stub.

namespace stub {
namespace v4 {

class YAMLDocumentHandler : public DocumentHandler {
  bool canRead(MemoryBufferRef memBufferRef,
               FileType types = FileType::All) const override;
  FileType getFileType(MemoryBufferRef memBufferRef) const override;
  bool canWrite(const InterfaceFile *file,
                VersionedFileType fileType) const override;
  bool handleDocument(llvm::yaml::IO &io,
                      const InterfaceFile *&file) const override;
};

} // end namespace v4.
} // end namespace stub.

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_TEXT_STUB_H
