//===- tapi/Binary/MachOReader - TAPI MachO Reader --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the MachO Reader.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_BINARY_MACHO_READER_H
#define TAPI_BINARY_MACHO_READER_H

#include "tapi/Core/API.h"
#include "tapi/Core/ArchitectureSet.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

struct MachOParseOption {
  ArchitectureSet arches = ArchitectureSet::All();
  bool parseMachOHeader = true;
  bool parseSymbolTable = true;
  bool parseObjCMetadata = false;
  bool parseUndefined = true;
};

/// Returns macho file type. Unknown if the format is not supported.
llvm::Expected<FileType> getMachOFileType(llvm::MemoryBufferRef bufferRef);

using MachOParseResult = std::vector<std::pair<Architecture, API>>;

/// Read APIs from the macho buffer.
llvm::Expected<MachOParseResult> readMachOFile(llvm::MemoryBufferRef memBuffer,
                                               MachOParseOption &option);

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_BINARY_MACHO_READER_H
