//===- lib/Core/Registry.cpp - TAPI Registry --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the TAPI Registry.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/Registry.h"
#include "tapi/Core/MachODylibReader.h"
#include "tapi/Core/TextStub.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

namespace {

// Diagnostic reader. It can read all the YAML file with !tapi tag and returns
// proper error when started to read the file.
class DiagnosticReader : public Reader {
  bool canRead(file_magic fileType, MemoryBufferRef bufferRef,
               FileType types = FileType::All) const override;
  Expected<FileType> getFileType(file_magic magic,
                                 MemoryBufferRef bufferRef) const override;
  Expected<std::unique_ptr<InterfaceFile>>
  readFile(std::unique_ptr<MemoryBuffer> memBuffer, ReadFlags readFlags,
           ArchitectureSet arches) const override;
};

} // namespace

bool DiagnosticReader::canRead(file_magic fileType, MemoryBufferRef bufferRef,
                               FileType types) const {
  auto str = bufferRef.getBuffer().trim();
  if (!str.startswith("--- !tapi") || !str.endswith("..."))
    return false;

  return true;
}

Expected<FileType>
DiagnosticReader::getFileType(file_magic magic,
                              MemoryBufferRef bufferRef) const {
  return Invalid;
}

Expected<std::unique_ptr<InterfaceFile>>
DiagnosticReader::readFile(std::unique_ptr<MemoryBuffer> memBuffer,
                           ReadFlags readFlags, ArchitectureSet arches) const {
  auto str = memBuffer->getBuffer().trim();
  auto tag = str.split('\n').first.drop_front(4);
  return make_error<StringError>(
      "unsupported tapi file type \'" + tag.str() + "\' in YAML",
      std::make_error_code(std::errc::not_supported));
}

bool Registry::canRead(MemoryBufferRef memBuffer, FileType types) const {
  auto data = memBuffer.getBuffer();
  auto magic = identify_magic(data);

  for (const auto &reader : _readers) {
    if (reader->canRead(magic, memBuffer, types))
      return true;
  }

  return false;
}

Expected<FileType> Registry::getFileType(MemoryBufferRef memBuffer) const {
  auto data = memBuffer.getBuffer();
  auto magic = identify_magic(data);

  for (const auto &reader : _readers) {
    auto fileType = reader->getFileType(magic, memBuffer);
    if (!fileType)
      return fileType.takeError();
    if (fileType.get() != FileType::Invalid)
      return fileType;
  }

  return FileType::Invalid;
}

bool Registry::canWrite(const InterfaceFile *file,
                        VersionedFileType fileType) const {
  for (const auto &writer : _writers) {
    if (writer->canWrite(file, fileType))
      return true;
  }

  return false;
}

Expected<std::unique_ptr<InterfaceFile>>
Registry::readFile(std::unique_ptr<MemoryBuffer> memBuffer, ReadFlags readFlags,
                   ArchitectureSet arches) const {
  auto data = memBuffer->getBuffer();
  auto fileType = identify_magic(data);

  for (const auto &reader : _readers) {
    if (!reader->canRead(fileType, memBuffer->getMemBufferRef()))
      continue;
    return reader->readFile(std::move(memBuffer), readFlags, arches);
  }

  return make_error<StringError>(
      "unsupported file type", std::make_error_code(std::errc::not_supported));
}

Error Registry::writeFile(const std::string &path, const InterfaceFile *file,
                          VersionedFileType fileType) const {
  std::error_code ec;
  {
    raw_fd_ostream os(path, ec, sys::fs::F_Text);
    if (ec)
      return errorCodeToError(ec);
    auto error = writeFile(os, file, fileType);
    if (error)
      return error;
  }
  if (ec)
    return errorCodeToError(ec);
  return Error::success();
}

Error Registry::writeFile(raw_ostream &os, const InterfaceFile *file,
                          VersionedFileType fileType) const {
  for (const auto &writer : _writers) {
    if (!writer->canWrite(file, fileType))
      continue;
    return writer->writeFile(os, file, fileType);
  }

  return make_error<StringError>(
      "unsupported file type", std::make_error_code(std::errc::not_supported));
}

void Registry::addBinaryReaders() {
  add(std::unique_ptr<Reader>(new MachODylibReader));
}

void Registry::addYAMLReaders() {
  auto reader = make_unique<YAMLReader>();
  reader->add(
      std::unique_ptr<DocumentHandler>(new stub::v1::YAMLDocumentHandler));
  reader->add(
      std::unique_ptr<DocumentHandler>(new stub::v2::YAMLDocumentHandler));
  reader->add(
      std::unique_ptr<DocumentHandler>(new stub::v3::YAMLDocumentHandler));
  reader->add(
      std::unique_ptr<DocumentHandler>(new stub::v4::YAMLDocumentHandler));
  add(std::unique_ptr<Reader>(std::move(reader)));
}

void Registry::addYAMLWriters() {
  auto writer = make_unique<YAMLWriter>();
  writer->add(
      std::unique_ptr<DocumentHandler>(new stub::v1::YAMLDocumentHandler));
  writer->add(
      std::unique_ptr<DocumentHandler>(new stub::v2::YAMLDocumentHandler));
  writer->add(
      std::unique_ptr<DocumentHandler>(new stub::v3::YAMLDocumentHandler));
  writer->add(
      std::unique_ptr<DocumentHandler>(new stub::v4::YAMLDocumentHandler));
  add(std::unique_ptr<Writer>(std::move(writer)));
}

void Registry::addDiagnosticReader() {
  add(make_unique<DiagnosticReader>());
}

TAPI_NAMESPACE_INTERNAL_END
