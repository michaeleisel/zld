//===- lib/Core/FileManager.cpp - File Manager ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the file manager.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/FileManager.h"
#include "tapi/Defines.h"
#include "clang/Basic/FileSystemStatCache.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/VirtualFileSystem.h"

using namespace llvm;
using namespace clang;

TAPI_NAMESPACE_INTERNAL_BEGIN

FileManager::FileManager(
    const FileSystemOptions &fileSystemOpts,
    IntrusiveRefCntPtr<FileSystemStatCacheFactory> cacheFactory,
    IntrusiveRefCntPtr<vfs::FileSystem> fs)
    : clang::FileManager(fileSystemOpts, fs), cacheFactory(cacheFactory) {
  // Record if initialized with VFS.
  if (fs)
    initWithVFS = true;

  // Inject our stat cache.
  installStatRecorder();
}

bool FileManager::exists(StringRef path) {
  llvm::vfs::Status result;
  if (getNoncachedStatValue(path, result))
    return false;
  return result.exists();
}

bool FileManager::isSymlink(StringRef path) {
  if (initWithVFS)
    return false;
  return sys::fs::is_symlink_file(path);
}

void FileManager::installStatRecorder() {
  clearStatCache();
  if (cacheFactory != nullptr)
    setStatCache(std::unique_ptr<FileSystemStatCache>(cacheFactory->create()));
}

TAPI_NAMESPACE_INTERNAL_END
