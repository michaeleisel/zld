//===- lib/Core/FakeSymbols.cpp - Fake Symbols ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Fake several LLVM method. This prevents the inclusion of static
/// initializers code that we don't need.
///
//===----------------------------------------------------------------------===//

#include "llvm/Object/Archive.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/WindowsResource.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"

namespace llvm {
class MemoryBufferRef;
} // namespace llvm

namespace llvm {

namespace object {

Expected<std::unique_ptr<Archive>> Archive::create(MemoryBufferRef Source) {
  llvm_unreachable("not supported");
}

Expected<std::unique_ptr<WindowsResource>>
WindowsResource::createWindowsResource(MemoryBufferRef Source) {
  llvm_unreachable("not supported");
}

Expected<std::unique_ptr<COFFObjectFile>>
ObjectFile::createCOFFObjectFile(MemoryBufferRef Object) {
  llvm_unreachable("not supported");
}

Expected<std::unique_ptr<ObjectFile>>
ObjectFile::createELFObjectFile(MemoryBufferRef Obj) {
  llvm_unreachable("not supported");
}

Expected<std::unique_ptr<WasmObjectFile>>
ObjectFile::createWasmObjectFile(MemoryBufferRef Object) {
  llvm_unreachable("not supported");
}

Expected<MemoryBufferRef>
IRObjectFile::findBitcodeInObject(const ObjectFile &Obj) {
  llvm_unreachable("not supported");
}

Expected<std::unique_ptr<IRObjectFile>>
IRObjectFile::create(MemoryBufferRef Object, LLVMContext &Context) {
  llvm_unreachable("not supported");
}

} // end namespace object.
} // end namespace llvm.
