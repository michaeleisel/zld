//===- tapi/Core/YAMLReaderWriter.cpp - YAML Reader/Writer ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the YAML reader/writer.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/YAMLReaderWriter.h"
#include "tapi/Core/Registry.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::yaml;
using namespace tapi::internal;

namespace llvm {
namespace yaml {

template <> struct DocumentListTraits<std::vector<const InterfaceFile *>> {
  static size_t size(IO &io, std::vector<const InterfaceFile *> &seq) {
    return seq.size();
  }
  static const InterfaceFile *&
  element(IO &io, std::vector<const InterfaceFile *> &seq, size_t index) {
    if (index >= seq.size())
      seq.resize(index + 1);
    return seq[index];
  }
};

template <> struct MappingTraits<const InterfaceFile *> {
  static void mapping(IO &io, const InterfaceFile *&file) {
    auto ctx = reinterpret_cast<YAMLContext *>(io.getContext());
    assert(ctx != nullptr);
    ctx->base.handleDocument(io, file);
  }
};
} // namespace yaml
} // namespace llvm

TAPI_NAMESPACE_INTERNAL_BEGIN

static void DiagHandler(const SMDiagnostic &diag, void *context) {
  auto *file = static_cast<YAMLContext *>(context);
  SmallString<1024> message;
  raw_svector_ostream s(message);

  SMDiagnostic newdiag(*diag.getSourceMgr(), diag.getLoc(), file->path,
                       diag.getLineNo(), diag.getColumnNo(), diag.getKind(),
                       diag.getMessage(), diag.getLineContents(),
                       diag.getRanges(), diag.getFixIts());

  newdiag.print(nullptr, s);
  file->errorMessage = message.str();
}

bool YAMLBase::canRead(MemoryBufferRef memBufferRef, FileType types) const {
  for (const auto &handler : _documentHandlers) {
    if (handler->canRead(memBufferRef, types))
      return true;
  }
  return false;
}

bool YAMLBase::canWrite(const InterfaceFile *file,
                        VersionedFileType fileType) const {
  for (const auto &handler : _documentHandlers) {
    if (handler->canWrite(file, fileType))
      return true;
  }
  return false;
}

FileType YAMLBase::getFileType(MemoryBufferRef bufferRef) const {
  for (const auto &handler : _documentHandlers) {
    auto fileType = handler->getFileType(bufferRef);
    if (fileType != FileType::Invalid)
      return fileType;
  }
  return FileType::Invalid;
}

bool YAMLBase::handleDocument(IO &io, const InterfaceFile *&file) const {
  for (const auto &handler : _documentHandlers) {
    if (handler->handleDocument(io, file))
      return true;
  }
  return false;
}

bool YAMLReader::canRead(file_magic magic, MemoryBufferRef memBufferRef,
                         FileType types) const {
  return YAMLBase::canRead(memBufferRef, types);
}

Expected<FileType> YAMLReader::getFileType(file_magic magic,
                                           MemoryBufferRef memBufferRef) const {
  return YAMLBase::getFileType(memBufferRef);
}

Expected<std::unique_ptr<InterfaceFile>>
YAMLReader::readFile(std::unique_ptr<MemoryBuffer> memBuffer,
                     ReadFlags readFlags, ArchitectureSet arches) const {
  // Create YAML Input Reader.
  YAMLContext ctx(*this);
  ctx.path = memBuffer->getBufferIdentifier();
  ctx.readFlags = readFlags;
  llvm::yaml::Input yin(memBuffer->getBuffer(), &ctx, DiagHandler, &ctx);

  // Fill vector with File objects created by parsing yaml.
  std::vector<const InterfaceFile *> files;
  yin >> files;

  if (yin.error())
    return make_error<StringError>("malformed file\n" + ctx.errorMessage,
                                   yin.error());

  if (files.empty())
    return errorCodeToError(std::make_error_code(std::errc::not_supported));

  auto *file = const_cast<InterfaceFile *>(files.front());
  file->setMemoryBuffer(std::move(memBuffer));

  for (auto it = std::next(files.begin()); it != files.end(); ++it) {
    auto *document = const_cast<InterfaceFile *>(*it);
    file->addDocument(std::unique_ptr<InterfaceFile>(document));
  }

  return std::unique_ptr<InterfaceFile>(file);
}

bool YAMLWriter::canWrite(const InterfaceFile *file,
                          VersionedFileType fileType) const {
  return YAMLBase::canWrite(file, fileType);
}

Error YAMLWriter::writeFile(raw_ostream &os, const InterfaceFile *file,
                            VersionedFileType fileType) const {
  if (file == nullptr)
    return errorCodeToError(std::make_error_code(std::errc::invalid_argument));

  YAMLContext ctx(*this);
  ctx.path = file->getPath();
  ctx.fileType = fileType;
  llvm::yaml::Output yout(os, &ctx, /*WrapColumn=*/80);

  std::vector<const InterfaceFile *> files;
  files.emplace_back(file);

  for (auto &it : file->_documents)
    files.emplace_back(it.get());

  // Stream out yaml.
  yout << files;

  return Error::success();
}

TAPI_NAMESPACE_INTERNAL_END
