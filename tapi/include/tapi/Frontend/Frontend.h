//===- tapi/Frontend/Frontend.h - TAPI Frontend -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the TAPI Frontend.
//===----------------------------------------------------------------------===//

#ifndef TAPI_FRONTEND_FRONTEND_H
#define TAPI_FRONTEND_FRONTEND_H

#include "tapi/Core/FileManager.h"
#include "tapi/Core/HeaderFile.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Core/Path.h"
#include "tapi/Defines.h"
#include "tapi/Frontend/FrontendContext.h"
#include "clang/Frontend/FrontendOptions.h"
#include "llvm/ADT/Triple.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

struct FrontendJob {
  std::string workingDirectory;
  IntrusiveRefCntPtr<FileSystemStatCacheFactory> cacheFactory;
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> vfs;
  llvm::Triple target;
  clang::InputKind::Language language = clang::InputKind::Unknown;
  bool useRTTI = true;
  bool enableModules = false;
  bool validateSystemHeaders = false;
  bool useObjectiveCARC = false;
  bool useObjectiveCWeakARC = false;
  bool useUmbrellaHeaderOnly = false;
  bool verbose = false;
  std::string language_std;
  std::string visibility;
  std::string isysroot;
  std::string moduleCachePath;
  std::string clangResourcePath;
  std::vector<std::pair<std::string, bool /*isUndef*/>> macros;
  HeaderSeq headerFiles;
  PathSeq systemFrameworkPaths;
  PathSeq systemIncludePaths;
  PathSeq frameworkPaths;
  PathSeq includePaths;
  std::vector<std::string> clangExtraArgs;
  HeaderType type;
  llvm::Optional<std::string> clangExecutablePath;
};

extern llvm::Optional<FrontendContext>
runFrontend(const FrontendJob &job, StringRef inputFilename = StringRef());

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_FRONTEND_FRONTEND_H
