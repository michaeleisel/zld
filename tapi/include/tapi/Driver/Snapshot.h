//===--- tapi/Driver/Snapshot.h - Options -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef TAPI_DRIVER_SNAPSHOT_H
#define TAPI_DRIVER_SNAPSHOT_H

#include "tapi/Core/LLVM.h"
#include "tapi/Core/Path.h"
#include "tapi/Defines.h"
#include "tapi/Driver/Options.h"
#include "tapi/Driver/SnapshotFileSystem.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ManagedStatic.h"
#include <map>
#include <string>
#include <vector>

namespace llvm {
namespace yaml {
template <typename T> struct MappingTraits;
}
}

TAPI_NAMESPACE_INTERNAL_BEGIN

using FileMapping = std::map<std::string, std::string>;
using SymlinkMapping = std::map<std::string, std::string>;
using DirectorySet = std::set<std::string>;

/// \brief A snapshot records all options and files that are accessed during a
///        TAPI invocation and stored to disk.
class Snapshot {
public:
  /// \brief Default constructor.
  Snapshot();

  /// \brief The destructor writes the snapshot to disk (if requested).
  ~Snapshot();

  /// \brief Loads a snapshot from the specified path and creates a virtual
  ///        file system that points to the content in snapshot directory.
  bool loadSnapshot(StringRef path);

  /// \brief Records the raw command line arguments.
  ///
  /// This is for logging purposes only.
  void recordRawArguments(ArrayRef<const char *> args);

  /// \brief Sets the name of the snapshot.
  void setName(StringRef name) { this->name = name; }

  /// \brief Specify the directory where to store the snapshot.
  void setRootPath(StringRef path) { rootPath = path; }

  /// \brief Record all the options.
  ///
  /// This method should be invoked once all the command line arguments have
  /// been processed.
  void recordOptions(const Options &options);

  /// \brief Record the path of a file that should be preserved by the snapshot.
  ///
  /// This only records the path of the file. The file will only be preserved
  /// when the snapshot is created.
  void recordFile(StringRef path);

  /// \brief Record the path of a directory.
  ///
  /// This only records the path of the directory. The directory will only be
  /// preserved when the snapshot is created. This doesn't preserve the content
  /// of the directory.
  void recordDirectory(StringRef path);

  /// \brief Indicate that we want a snapshot to be created when the global
  ///        snaphot instance is destroyed.
  void requestSnapshot() { wantSnapshot = true; }

  /// \brief Force the immediate emission of a snapshot to disk.
  ///
  /// This is intended to be called during a crash.
  void writeSnapshot(bool isCrash = true);

  /// \brief Provides the snapshot file system after a snapshot has been loaded
  ///        from disk.
  IntrusiveRefCntPtr<SnapshotFileSystem> getVirtualFileSystem() const {
    return fs;
  }

  /// \brief Set current working directory.
  void setWorkingDirectory(StringRef path) { workingDirectory = path; }

  StringRef getWorkingDirectory() { return workingDirectory; }

  struct MappingContext {
    ArchitectureSet architectures;
    Platform platform = Platform::unknown;
    std::string osVersion;
  };

private:
  // Options we need to preserve.
  TAPICommand command;
  DriverOptions driverOptions;
  ArchiveOptions archiveOptions;
  LinkerOptions linkerOptions;
  FrontendOptions frontendOptions;
  DiagnosticsOptions diagnosticsOptions;
  TAPIOptions tapiOptions;

  /// \brief The snapshot file system that is generated for loaded snapshots.
  IntrusiveRefCntPtr<SnapshotFileSystem> fs;

  FileMapping pathToHash;
  SymlinkMapping symlinkToPath;
  DirectorySet directorySet;
  PathSeq files;
  PathSeq directories;
  PathSeq normalizedDirectories;
  std::vector<std::string> rawArgs;
  std::string rootPath = "/tmp/tapi-snapshot";
  std::string name = "tapi";
  std::string workingDirectory;
  std::string tapiVersion;
  bool wantSnapshot = false;
  bool snapshotWritten = false;
  MappingContext context;

  void findAndRecordSymlinks(SmallVectorImpl<char> &path, int level);

  template <typename T> friend struct llvm::yaml::MappingTraits;
  friend class Options;
};

extern llvm::ManagedStatic<Snapshot> globalSnapshot;

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_DRIVER_SNAPSHOT_H
