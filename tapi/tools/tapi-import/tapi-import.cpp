//===- tools/tapi-import/tapi-import.cpp - TAPI Import Tool -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// The tapi import tool to ingest a SDK.
///
//===----------------------------------------------------------------------===//
#include "tapi/Core/FileManager.h"
#include "tapi/Core/FileSystem.h"
#include "tapi/Core/InterfaceFile.h"
#include "tapi/Core/Path.h"
#include "tapi/Core/Registry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/Signals.h"
#include <map>
#include <string>

using namespace llvm;
using namespace tapi::internal;

static cl::OptionCategory tapiCategory("tapi-import options");

static cl::opt<std::string> input(cl::Positional, cl::desc("<directory>"),
                                  cl::cat(tapiCategory));
static cl::opt<std::string> output("o", cl::desc("output file"),
                                   cl::cat(tapiCategory));
static cl::opt<std::string> prefix("prefix", cl::desc("variable prefix"),
                                   cl::init("tbd"), cl::cat(tapiCategory));

namespace {
struct Context {
  IntrusiveRefCntPtr<FileManager> fm;
  Registry registry;
  SmallString<PATH_MAX> inputPath;
  SmallString<PATH_MAX> outputPath;
  std::map<std::string, std::string> normalizedPathToVarName;

  Context() : fm(new FileManager(clang::FileSystemOptions())) {
    registry.addYAMLReaders();
    registry.addBinaryReaders();
  }
};
} // end anonymous namespace.

static Error printTBDFile(raw_ostream &sdkFile, Context &ctx,
                          const InterfaceFile *tbd, StringRef normalizedPath) {
  static int num = 0;
  std::string varName = prefix + std::to_string(num++);

  if (ctx.normalizedPathToVarName.count(normalizedPath))
    return make_error<StringError>("normalizedPath already exists in map: " +
                                       tbd->getInstallName(),
                                   inconvertibleErrorCode());
  ctx.normalizedPathToVarName[normalizedPath] = varName;

  sdkFile << "// BEGIN " << tbd->getInstallName() << "\n";
  if (!tbd->uuids().empty()) {
    sdkFile << "static constexpr Reference " << varName << "_uuids[] = {";
    for (const auto &uuid : tbd->uuids()) {
      sdkFile << "\n";
      sdkFile.indent(4) << "{ \"" << uuid.second << "\", "
                        << (unsigned)uuid.first.architecture << "U },";
    }
    sdkFile << "\n};\n";
    sdkFile << "static constexpr size_t " << varName
            << "_uuidsSize = llvm::array_lengthof(" << varName
            << "_uuids);\n\n";
  }

  if (!tbd->allowableClients().empty()) {
    sdkFile << "static constexpr Reference " << varName
            << "_allowableClient[] = {";
    for (const auto &lib : tbd->allowableClients()) {
      sdkFile << "\n";
      sdkFile.indent(4) << "{ \"" << lib.getInstallName() << "\", "
                        << lib.getArchitectures().rawValue() << " },";
    }
    sdkFile << "\n};\n";
    sdkFile << "static constexpr size_t " << varName
            << "_allowableClientSize = llvm::array_lengthof(" << varName
            << "_allowableClient);\n\n";
  }

  if (!tbd->reexportedLibraries().empty()) {
    sdkFile << "static constexpr Reference " << varName
            << "_reexportedLibraries[] = {";
    for (const auto &lib : tbd->reexportedLibraries()) {
      sdkFile << "\n";
      sdkFile.indent(4) << "{ \"" << lib.getInstallName() << "\", "
                        << lib.getArchitectures().rawValue() << " },";
    }
    sdkFile << "\n};\n";
    sdkFile << "static constexpr size_t " << varName
            << "_reexportedLibrariesSize = llvm::array_lengthof(" << varName
            << "_reexportedLibraries);\n\n";
  }

  if (tbd->exports().begin() != tbd->exports().end()) {
    sdkFile << "static constexpr Symbol " << varName << "_exports[] = {";
    for (const auto *symbol : tbd->exports()) {
      sdkFile << "\n";
      sdkFile.indent(4) << "{ \"" << symbol->getName() << "\", "
                        << symbol->getArchitectures().rawValue() << "U, "
                        << (unsigned)symbol->getKind() << "U, "
                        << (unsigned)symbol->getFlags() << "U },";
    }
    sdkFile << "\n};\n";
    sdkFile << "static constexpr size_t " << varName
            << "_exportsSize = llvm::array_lengthof(" << varName
            << "_exports);\n\n";
  }

  if (tbd->undefineds().begin() != tbd->undefineds().end()) {
    sdkFile << "static constexpr Symbol " << varName << "undefineds[] = {";
    for (const auto *symbol : tbd->undefineds()) {
      sdkFile << "\n";
      sdkFile.indent(4) << "{ \"" << symbol->getName() << "\", "
                        << symbol->getArchitectures().rawValue() << "U, "
                        << (unsigned)symbol->getKind() << "U, "
                        << (unsigned)symbol->getFlags() << "U },";
    }
    sdkFile << "\n};\n";
    sdkFile << "static constexpr size_t " << varName
            << "_undefinedsSize = llvm::array_lengthof(" << varName
            << "_undefineds);\n\n";
  }

  sdkFile << "static constexpr SDKMetadata " << varName << " = {\n";

  if (!tbd->allowableClients().empty())
    sdkFile.indent(4) << "{ " << varName << "_allowableClient, " << varName
                      << "_allowableClientSize },\n";
  else
    sdkFile.indent(4) << "{ nullptr, 0U },\n";

  if (!tbd->reexportedLibraries().empty())
    sdkFile.indent(4) << "{ " << varName << "_reexportedLibraries, " << varName
                      << "_reexportedLibrariesSize },\n";
  else
    sdkFile.indent(4) << "{ nullptr, 0U },\n";

  if (!tbd->uuids().empty())
    sdkFile.indent(4) << "{ " << varName << "_uuids, " << varName
                      << "_uuidsSize },\n";
  else
    sdkFile.indent(4) << "{ nullptr, 0U },\n";

  if (tbd->exports().begin() != tbd->exports().end())
    sdkFile.indent(4) << "{ " << varName << "_exports, " << varName
                      << "_exportsSize },\n";
  else
    sdkFile.indent(4) << "{ nullptr, 0U },\n";

  if (tbd->undefineds().begin() != tbd->undefineds().end())
    sdkFile.indent(4) << "{ " << varName << "_undefineds, " << varName
                      << "_undefinedsSize },\n";
  else
    sdkFile.indent(4) << "{ nullptr, 0U },\n";

  sdkFile.indent(4) << "\"" << tbd->getInstallName() << "\",\n";
  sdkFile.indent(4) << "\""
                    << (tbd->umbrellas().empty()
                            ? ""
                            : tbd->umbrellas().front().second)
                    << "\",\n";
  sdkFile.indent(4) << tbd->getCurrentVersion()._version << ",\n";
  sdkFile.indent(4) << tbd->getCompatibilityVersion()._version << ",\n";
  sdkFile.indent(4) << tbd->getArchitectures().rawValue() << ",\n";
  sdkFile.indent(4) << (unsigned)*tbd->getPlatforms().begin() << "U,\n";
  sdkFile.indent(4) << (unsigned)tbd->getSwiftABIVersion() << "U,\n";
  sdkFile.indent(4) << (unsigned)0 << "U,\n";
  sdkFile.indent(4) << static_cast<int>(tbd->isTwoLevelNamespace()) << ",\n";
  sdkFile.indent(4) << static_cast<int>(tbd->isApplicationExtensionSafe())
                    << ",\n";
  sdkFile.indent(4) << static_cast<int>(tbd->isInstallAPI()) << ",\n";

  sdkFile << "};\n";

  sdkFile << "// END " << tbd->getInstallName() << "\n\n";

  return Error::success();
}

static Error importSDK(Context &ctx) {
  assert(ctx.inputPath.back() != '/' && "Unexpected / at end of input path.");

  std::error_code io_ec;
  raw_fd_ostream sdkFile(ctx.outputPath, io_ec,
                         sys::fs::FA_Read | sys::fs::FA_Write);
  if (io_ec)
    return errorCodeToError(io_ec);

  std::map<std::string, std::unique_ptr<InterfaceFile>> dylibs;
  std::error_code ec;
  for (sys::fs::recursive_directory_iterator i(ctx.inputPath, ec), ie; i != ie;
       i.increment(ec)) {

    if (ec)
      return errorCodeToError(ec);

    // Skip header directories (include/Headers/PrivateHeaders), module
    // files, and tolchain directories.
    StringRef path = i->path();
    if (path.endswith("/include") || path.endswith("/Headers") ||
        path.endswith("/PrivateHeaders") || path.endswith("/Modules") ||
        path.endswith(".map") || path.endswith(".modulemap") ||
        path.endswith(".xctoolchain")) {
      i.no_push();
      continue;
    }

    // Check if the entry is a symlink, because we don't follow symlinks.
    bool isSymlink;
    if (auto ec = sys::fs::is_symlink_file(path, isSymlink))
      return errorCodeToError(ec);

    if (isSymlink) {
      // Don't follow symlink.
      i.no_push();
      continue;
    }

    // We only have to look at files.
    auto *file = ctx.fm->getFile(path);
    if (!file)
      continue;

    auto bufferOrErr = ctx.fm->getBufferForFile(file);
    if (auto ec = bufferOrErr.getError())
      return errorCodeToError(ec);

    // Check for dynamic libs and text-based stub files.
    if (!ctx.registry.canRead(bufferOrErr.get()->getMemBufferRef()))
      continue;

    auto interface =
        ctx.registry.readFile(std::move(bufferOrErr.get()), ReadFlags::Symbols);
    if (!interface)
      return interface.takeError();

    // Ignore anything that is not iOS.
    if (interface.get()->getPlatforms().count(Platform::macOS))
      continue;

    // Don't import 64bit only dylibs.
    auto archs = interface.get()->getArchitectures();
    if (!archs.has(AK_i386) && !archs.has(AK_armv7) && !archs.has(AK_armv7s))
      continue;

    // Normalize path for map lookup by removing the extension.
    SmallString<PATH_MAX> normalizedPath(path);
    TAPI_INTERNAL::replace_extension(normalizedPath, "");

    if ((interface.get()->getFileType() == FileType::MachO_DynamicLibrary) ||
        (interface.get()->getFileType() ==
         FileType::MachO_DynamicLibrary_Stub)) {
      // Don't add this MachO dynamic library, because we already have a
      // text-based stub recorded for this path.
      if (dylibs.count(normalizedPath.c_str()))
        continue;
    }

    // FIXME: Once we use C++17, this can be simplified.
    auto it = dylibs.find(normalizedPath.c_str());
    if (it != dylibs.end())
      it->second = std::move(interface.get());
    else
      dylibs.emplace(std::piecewise_construct,
                     std::forward_as_tuple(normalizedPath.c_str()),
                     std::forward_as_tuple(std::move(interface.get())));
  }

  for (auto &it : dylibs) {
    auto &dylib = it.second;
    SmallString<PATH_MAX> input(dylib->getPath());
    SmallString<PATH_MAX> output = input;
    sys::path::replace_extension(output, ".tbd");

    std::string normalizedPath = it.first;
    normalizedPath.erase(0, ctx.inputPath.size());
    normalizedPath =
        Regex("[0-9]+.[0-9]+(.Internal)?.sdk").sub(".sdk", normalizedPath);

    if (auto error = printTBDFile(sdkFile, ctx, dylib.get(), normalizedPath))
      return error;
  }

  sdkFile << "static constexpr LookupTableEntry lookupTable[] = {\n";
  for (const auto &it : ctx.normalizedPathToVarName)
    sdkFile.indent(4) << "{ \"" << it.first << "\", &" << it.second << " },\n";
  sdkFile << "};\n\n";

  return Error::success();
}

int main(int argc, const char *argv[]) {
  // Standard set up, so program fails gracefully.
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  llvm::PrettyStackTraceProgram stackPrinter(argc, argv);
  llvm::llvm_shutdown_obj shutdown;

  if (llvm::sys::Process::FixupStandardFileDescriptors())
    return 1;

  cl::HideUnrelatedOptions(tapiCategory);
  cl::ParseCommandLineOptions(argc, argv, "TAPI Import Tool\n");

  if (input.empty()) {
    cl::PrintHelpMessage();
    return 0;
  }

  if (output.empty()) {
    errs() << "error: no output file specified\n";
    return 1;
  }

  Context ctx;
  ctx.inputPath = input;
  ctx.outputPath = output;
  if (auto ec = realpath(ctx.inputPath)) {
    errs() << "error: " << ctx.inputPath << ec.message() << "\n";
    return 1;
  }

  bool isDirectory = false;
  if (auto ec = sys::fs::is_directory(ctx.inputPath, isDirectory)) {
    errs() << "error: " << ctx.inputPath << ec.message() << "\n";
    return 1;
  }

  if (!isDirectory) {
    errs() << "error: not a directory: " << ctx.inputPath << "\n";
    return 1;
  }

  auto result = importSDK(ctx);
  if (result) {
    errs() << "error: " << toString(std::move(result)) << "\n";
    return 1;
  }

  return 0;
}
