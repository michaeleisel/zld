//===- tapi/Core/MachODylibReader.h - TAPI MachO Dylib Reader ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the MachO Dynamic Library Reader.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_MACHO_DYLIB_READER_H
#define TAPI_CORE_MACHO_DYLIB_READER_H

#include "tapi/Core/ArchitectureSet.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Core/Registry.h"
#include "tapi/Defines.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

class MachODylibReader final : public Reader {
public:
  bool canRead(file_magic magic, MemoryBufferRef bufferRef,
               FileType types) const override;
  Expected<FileType> getFileType(file_magic magic,
                                 MemoryBufferRef bufferRef) const override;
  Expected<std::unique_ptr<InterfaceFile>>
  readFile(std::unique_ptr<MemoryBuffer> memBuffer, ReadFlags readFlags,
           ArchitectureSet arches) const override;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_MACHO_DYLIB_READER_H
