//===- tapi/Driver/SnapshotFileSystem.h - Snapshot File System --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the Snapshot Virtual File System.
///
//===----------------------------------------------------------------------===//
#ifndef TAPI_DRIVER_SNAPSHOT_FILE_SYSTEM_H
#define TAPI_DRIVER_SNAPSHOT_FILE_SYSTEM_H

#include "tapi/Core/LLVM.h"
#include "tapi/Defines.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <string>

using llvm::vfs::Status;
using llvm::vfs::File;
using llvm::vfs::directory_iterator;

TAPI_NAMESPACE_INTERNAL_BEGIN

/// \brief The snapshot virtual file system.
class SnapshotFileSystem final : public llvm::vfs::FileSystem {
private:
  enum class EntryKind { Directory, File, Symlink };

  class Entry {
    EntryKind kind;
    std::string name;

  public:
    virtual ~Entry();
    Entry(EntryKind kind, StringRef name) : kind(kind), name(name) {}
    StringRef getName() const { return name; }
    EntryKind getKind() const { return kind; }
  };

  class DirectoryEntry final : public Entry {
    std::vector<std::unique_ptr<Entry>> contents;
    Status status;

  public:
    DirectoryEntry(StringRef name)
        : Entry(EntryKind::Directory, name),
          status(name, llvm::vfs::getNextVirtualUniqueID(),
                 llvm::sys::TimePoint<>(), 0, 0, 0,
                 llvm::sys::fs::file_type::directory_file,
                 llvm::sys::fs::all_all) {}

    Status getStatus() { return status; }

    Entry *addContent(std::unique_ptr<Entry> content);

    using iterator = decltype(contents)::iterator;
    iterator contents_begin() { return contents.begin(); }
    iterator contents_end() { return contents.end(); }
    const std::vector<std::unique_ptr<Entry>> &content() const {
      return contents;
    };
    static bool classof(const Entry *entry) {
      return entry->getKind() == EntryKind::Directory;
    }
  };

  class FileEntry final : public Entry {
  private:
    std::string externalPath;

  public:
    FileEntry(StringRef name, StringRef externalPath)
        : Entry(EntryKind::File, name), externalPath(externalPath) {}

    StringRef getExternalPath() const { return externalPath; }

    static bool classof(const Entry *entry) {
      return entry->getKind() == EntryKind::File;
    }
  };

  class SymlinkEntry final : public Entry {
  private:
    std::string linkPath;

  public:
    SymlinkEntry(StringRef name, StringRef linkPath)
        : Entry(EntryKind::Symlink, name), linkPath(linkPath) {}

    StringRef getLinkPath() const { return linkPath; }

    static bool classof(const Entry *entry) {
      return entry->getKind() == EntryKind::Symlink;
    }
  };

  ErrorOr<DirectoryEntry *> lookupOrCreate(StringRef name,
                                           DirectoryEntry *current = nullptr);

  ErrorOr<Entry *> lookupPath(StringRef path, Entry *current) const {
    return lookupPath(llvm::sys::path::begin(path), llvm::sys::path::end(path),
                      current);
  }
  ErrorOr<Entry *> lookupPath(llvm::sys::path::const_iterator start,
                              llvm::sys::path::const_iterator end,
                              Entry *current) const;

  ErrorOr<Entry *> lookupPath(const Twine &path) const;

  llvm::ErrorOr<Status> status(const Twine &path, Entry *entry);

  void dumpEntry(raw_ostream &os, Entry *entry, unsigned indent = 0) const;

public:
  SnapshotFileSystem(IntrusiveRefCntPtr<FileSystem> externalFS =
                         llvm::vfs::getRealFileSystem())
      : root(llvm::make_unique<DirectoryEntry>("/")),
        externalFS(std::move(externalFS)) {}

  /// \brief Get the status of the entry at \p Path, if one exists.
  llvm::ErrorOr<Status> status(const Twine &path) override;

  /// \brief Get a \p File object for the file at \p Path, if one exists.
  llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>>
  openFileForRead(const Twine &path) override;

  /// \brief Get a directory_iterator for \p Dir.
  /// \note The 'end' iterator is directory_iterator().
  directory_iterator dir_begin(const Twine &dir, std::error_code &ec) override;

  /// Set the working directory. This will affect all following operations on
  /// this file system and may propagate down for nested file systems.
  std::error_code setCurrentWorkingDirectory(const Twine &path) override;

  /// Get the working directory of this file system.
  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const override;

  ErrorOr<DirectoryEntry *> addDirectory(StringRef path);

  ErrorOr<FileEntry *> addFile(StringRef path, StringRef externalPath);

  ErrorOr<SymlinkEntry *> addSymlink(StringRef path, StringRef linkPath);

  void dump(raw_ostream &os = llvm::dbgs()) const;

private:
  std::string workingDirectory;
  std::unique_ptr<DirectoryEntry> root;
  IntrusiveRefCntPtr<FileSystem> externalFS;

  friend class SnapshotDirIterImpl;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_DRIVER_SNAPSHOT_FILE_SYSTEM_H
