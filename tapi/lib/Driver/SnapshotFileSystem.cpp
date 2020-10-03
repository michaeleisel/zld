//===- lib/Scanner/SnapshotFileSystem.cpp - Snapshot File System-*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the TAPI Snapshot Virtual File System.
///
//===----------------------------------------------------------------------===//

#include "tapi/Driver/SnapshotFileSystem.h"
#include "tapi/Defines.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

SnapshotFileSystem::Entry::~Entry() = default;

ErrorOr<SnapshotFileSystem::Entry *>
SnapshotFileSystem::lookupPath(sys::path::const_iterator start,
                               sys::path::const_iterator end,
                               Entry *current) const {
  if (*start != current->getName())
    return make_error_code(llvm::errc::no_such_file_or_directory);

  if (auto *symlink = dyn_cast<SymlinkEntry>(current)) {
    auto result = lookupPath(symlink->getLinkPath());
    if (auto error = result.getError())
      return error;
    current = *result;
  }

  if (++start == end)
    return current;

  auto *directory = dyn_cast<DirectoryEntry>(current);
  if (!directory)
    return make_error_code(llvm::errc::not_a_directory);

  for (const auto &entry : directory->content()) {
    auto result = lookupPath(start, end, entry.get());
    if (result || result.getError() != llvm::errc::no_such_file_or_directory)
      return result;
  }
  return make_error_code(llvm::errc::no_such_file_or_directory);
}

ErrorOr<SnapshotFileSystem::Entry *>
SnapshotFileSystem::lookupPath(const Twine &path_) const {
  SmallString<PATH_MAX> path;
  path_.toVector(path);

  // Normalize path.
  if (auto ec = makeAbsolute(path))
    return ec;

  sys::path::remove_dots(path, /*remove_dot_dot=*/true);

  if (path.empty())
    return make_error_code(llvm::errc::invalid_argument);

  return lookupPath(path, root.get());
}

static Status getFileStatus(const Twine &path, const Status &externalStatus) {
  auto status = Status::copyWithNewName(externalStatus, path.str());
  status.IsVFSMapped = true;
  return status;
}

ErrorOr<Status> SnapshotFileSystem::status(const Twine &path, Entry *entry) {
  if (auto *file = dyn_cast<FileEntry>(entry)) {
    auto status = externalFS->status(file->getExternalPath());
    if (status)
      return getFileStatus(path, *status);
    return status;
  } // directory
  auto *directory = cast<DirectoryEntry>(entry);
  return Status::copyWithNewName(directory->getStatus(), path.str());
}

ErrorOr<Status> SnapshotFileSystem::status(const Twine &path) {
  auto result = lookupPath(path);
  if (auto ec = result.getError())
    return ec;
  return status(path, *result);
}

SnapshotFileSystem::Entry *SnapshotFileSystem::DirectoryEntry::addContent(
    std::unique_ptr<SnapshotFileSystem::Entry> content) {
  auto it = find_if(
      contents, [&content](std::unique_ptr<SnapshotFileSystem::Entry> &entry) {
        return (content->getKind() == entry->getKind()) &&
               (content->getName() == entry->getName());
      });
  if (it != contents.end()) {
    *it = std::move(content);
    return it->get();
  }
  contents.emplace_back(std::move(content));
  return contents.back().get();
}

namespace {
/// Provide a file wrapper with an overriden status.
class FileWithFixedStatus : public File {
  std::unique_ptr<File> file;
  Status status_;

public:
  FileWithFixedStatus(std::unique_ptr<File> file, Status status)
      : file(std::move(file)), status_(std::move(status)) {}

  ErrorOr<Status> status() override { return status_; }
  ErrorOr<std::unique_ptr<MemoryBuffer>> getBuffer(const Twine &Name,
                                                   int64_t FileSize,
                                                   bool RequiresNullTerminator,
                                                   bool IsVolatile) override {
    return file->getBuffer(Name, FileSize, RequiresNullTerminator, IsVolatile);
  }
  std::error_code close() override { return file->close(); }
};
} // end anonymous namespace

ErrorOr<std::unique_ptr<llvm::vfs::File>>
SnapshotFileSystem::openFileForRead(const Twine &path) {
  auto result = lookupPath(path);
  if (auto ec = result.getError())
    return ec;

  auto *file = dyn_cast<FileEntry>(*result);
  if (!file)
    return make_error_code(llvm::errc::invalid_argument);

  auto result2 = externalFS->openFileForRead(file->getExternalPath());
  if (auto ec = result2.getError())
    return ec;

  auto externalStatus = (*result2)->status();
  if (auto ec = externalStatus.getError())
    return ec;

  auto status = getFileStatus(path, *externalStatus);
  return std::unique_ptr<File>(
      make_unique<FileWithFixedStatus>(std::move(*result2), status));
}

class SnapshotDirIterImpl : public llvm::vfs::detail::DirIterImpl {
  std::string dir;
  SnapshotFileSystem &fs;
  SnapshotFileSystem::DirectoryEntry::iterator current, end;

public:
  SnapshotDirIterImpl(const Twine &path_, SnapshotFileSystem &fs,
                      SnapshotFileSystem::DirectoryEntry::iterator start,
                      SnapshotFileSystem::DirectoryEntry::iterator end,
                      std::error_code &ec)
      : dir(path_.str()), fs(fs), current(start), end(end) {
    if (current == end)
      return;

    SmallString<PATH_MAX> path;
    path_.toVector(path);
    sys::path::append(path, (*current)->getName());
    auto result = fs.status(path);
    if ((ec = result.getError()))
      return;

    CurrentEntry = {path.str(), result->getType()};
  }

  std::error_code increment() override {
    if (++current == end) {
      CurrentEntry = llvm::vfs::directory_entry();
      return {};
    }

    SmallString<PATH_MAX> path(dir);
    sys::path::append(path, (*current)->getName());
    auto result = fs.status(path);
    if (auto ec = result.getError())
      return ec;

    CurrentEntry = {path.str(), result->getType()};
    return {};
  }
};

directory_iterator SnapshotFileSystem::dir_begin(const Twine &dir,
                                                 std::error_code &ec) {
  auto result = lookupPath(dir);
  if ((ec = result.getError()))
    return directory_iterator();

  auto *directory = dyn_cast<DirectoryEntry>(*result);
  if (!directory) {
    ec = std::error_code(static_cast<int>(errc::not_a_directory),
                         std::system_category());
    return directory_iterator();
  }

  return directory_iterator(std::make_shared<SnapshotDirIterImpl>(
      dir, *this, directory->contents_begin(), directory->contents_end(), ec));
}

std::error_code
SnapshotFileSystem::setCurrentWorkingDirectory(const Twine &path_) {
  SmallString<PATH_MAX> path;
  path_.toVector(path);

  // Fix up relative paths. This just prepends the current working directory.
  auto ec = makeAbsolute(path);
  assert(!ec);
  (void)ec;

  llvm::sys::path::remove_dots(path, /*remove_dot_dot=*/true);

  if (!path.empty())
    workingDirectory = path.str();
  return {};
}

ErrorOr<std::string> SnapshotFileSystem::getCurrentWorkingDirectory() const {
  return workingDirectory;
}

ErrorOr<SnapshotFileSystem::FileEntry *>
SnapshotFileSystem::addFile(StringRef path, StringRef externalPath) {
  auto filename = sys::path::filename(path);
  auto parent = sys::path::parent_path(path);

  auto directory = addDirectory(parent);
  if (auto ec = directory.getError())
    return ec;
  return cast<FileEntry>(directory.get()->addContent(
      make_unique<FileEntry>(filename, externalPath)));
}

ErrorOr<SnapshotFileSystem::SymlinkEntry *>
SnapshotFileSystem::addSymlink(StringRef path, StringRef linkPath) {
  auto filename = sys::path::filename(path);
  auto parent = sys::path::parent_path(path);

  auto directory = addDirectory(parent);
  if (auto ec = directory.getError())
    return ec;
  return cast<SymlinkEntry>(directory.get()->addContent(
      make_unique<SymlinkEntry>(filename, linkPath)));
}

ErrorOr<SnapshotFileSystem::DirectoryEntry *>
SnapshotFileSystem::lookupOrCreate(StringRef name, DirectoryEntry *current) {
  if (current == nullptr) {
    if (root->getName() == name)
      return root.get();
    return make_error_code(llvm::errc::invalid_argument);
  }

  for (const auto &entry : current->content()) {
    if (entry->getName() == name)
      return cast<DirectoryEntry>(entry.get());
  }
  return cast<DirectoryEntry>(
      current->addContent(make_unique<DirectoryEntry>(name)));
}

ErrorOr<SnapshotFileSystem::DirectoryEntry *>
SnapshotFileSystem::addDirectory(StringRef path) {
  DirectoryEntry *currentDir = nullptr;
  for (auto start = sys::path::begin(path), end = sys::path::end(path);
       start != end; ++start) {
    auto result = lookupOrCreate(*start, currentDir);
    if (auto ec = result.getError())
      return ec;
    currentDir = result.get();
  }
  return currentDir;
}

LLVM_DUMP_METHOD void SnapshotFileSystem::dump(raw_ostream &os) const {
  dumpEntry(os, root.get());
}

LLVM_DUMP_METHOD void SnapshotFileSystem::dumpEntry(raw_ostream &os,
                                                    Entry *entry,
                                                    unsigned indent) const {
  os.indent(indent);
  if (auto *file = dyn_cast<FileEntry>(entry))
    os << file->getName() << " : " << file->getExternalPath() << "\n";
  else if (auto *symlink = dyn_cast<SymlinkEntry>(entry))
    os << symlink->getName() << " --> " << symlink->getLinkPath() << "\n";
  else
    os << entry->getName() << "/\n";

  if (auto *dir = dyn_cast<DirectoryEntry>(entry)) {
    for (auto &entry : dir->content())
      dumpEntry(os, entry.get(), indent + 2);
  }
}

TAPI_NAMESPACE_INTERNAL_END
