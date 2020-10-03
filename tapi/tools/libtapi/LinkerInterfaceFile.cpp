//===- libtapi/LinkerInterfaceFile.cpp - TAPI File Interface ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the C++ linker interface file API.
///
//===----------------------------------------------------------------------===//
#include "tapi/Core/InterfaceFile.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Core/Registry.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Object/MachO.h"
#include <string>
#include <tapi/LinkerInterfaceFile.h>
#include <tapi/PackedVersion32.h>
#include <vector>

using namespace llvm;

TAPI_NAMESPACE_V1_BEGIN

using namespace tapi::internal;

static PackedVersion32 parseVersion32(StringRef str) {
  uint32_t version = 0;
  if (str.empty())
    return 0;

  SmallVector<StringRef, 3> parts;
  SplitString(str, parts, ".");

  unsigned long long num = 0;
  if (getAsUnsignedInteger(parts[0], 10, num))
    return 0;

  if (num > UINT16_MAX)
    return 0;

  version = num << 16;

  if (parts.size() > 1) {
    if (getAsUnsignedInteger(parts[1], 10, num))
      return 0;

    if (num > UINT8_MAX)
      return 0;

    version |= (num << 8);
  }

  if (parts.size() > 2) {
    if (getAsUnsignedInteger(parts[2], 10, num))
      return 0;

    if (num > UINT8_MAX)
      return 0;

    version |= num;
  }

  return version;
}

class LLVM_LIBRARY_VISIBILITY LinkerInterfaceFile::Impl {
public:
  FileType _fileType{FileType::Unsupported};
  std::vector<uint32_t> _platforms;
  std::string _installName;
  std::string _parentFrameworkName;

  PackedVersion32 _currentVersion;
  PackedVersion32 _compatibilityVersion;
  unsigned _swiftABIVersion;
  bool _hasTwoLevelNamespace{false};
  bool _isAppExtensionSafe{false};
  bool _hasWeakDefExports{false};
  bool _installPathOverride{false};

  std::vector<std::string> _reexportedLibraries;
  std::vector<std::string> _allowableClients;
  std::vector<std::string> _ignoreExports;
  std::vector<std::string> _inlinedFrameworkNames;
  std::vector<Symbol> _exports;
  std::vector<Symbol> _undefineds;
  std::shared_ptr<const InterfaceFile> _interface;
  std::vector<std::shared_ptr<const InterfaceFile>> _inlinedFrameworks;

  Impl() noexcept = default;

  bool init(const std::shared_ptr<const InterfaceFile> &interface,
            cpu_type_t cpuType, cpu_subtype_t cpuSubType, ParsingFlags flags,
            PackedVersion32 minOSVersion, std::string &errorMessage) noexcept;

  template <typename T> void addSymbol(T &&name, APIFlags flags) {
    if (find(_ignoreExports, name) == _ignoreExports.end())
      _exports.emplace_back(std::forward<T>(name),
                            static_cast<SymbolFlags>(flags));
  }

  void processSymbol(StringRef name, PackedVersion32 minOSVersion,
                     bool disallowWeakImports) {
    // $ld$ <action> $ <condition> $ <symbol-name>
    if (!name.startswith("$ld$"))
      return;

    StringRef action, condition, symbolName;
    std::tie(action, name) = name.drop_front(4).split('$');
    std::tie(condition, symbolName) = name.split('$');
    if (action.empty() || condition.empty() || symbolName.empty())
      return;

    if (!condition.startswith("os"))
      return;

    auto version = parseVersion32(condition.drop_front(2));
    if (version != minOSVersion)
      return;

    if (action == "hide") {
      _ignoreExports.emplace_back(symbolName);
      return;
    }

    if (action == "add") {
      _exports.emplace_back(symbolName);
      return;
    }

    if (action == "weak") {
      if (disallowWeakImports)
        _ignoreExports.emplace_back(symbolName);

      return;
    }

    if (action == "install_name") {
      _installName = symbolName;
      _installPathOverride = true;
      if (_installName == "/System/Library/Frameworks/"
                          "ApplicationServices.framework/Versions/A/"
                          "ApplicationServices") {
        _compatibilityVersion = PackedVersion32(1, 0, 0);
      }
      return;
    }

    if (action == "compatibility_version") {
      _compatibilityVersion = parseVersion32(symbolName);
      return;
    }
  }
};

static Architecture getArchForCPU(cpu_type_t cpuType, cpu_subtype_t cpuSubType,
                                  bool enforceCpuSubType,
                                  ArchitectureSet archs) {
  // First check the exact cpu type and cpu sub type.
  auto arch = getArchType(cpuType, cpuSubType);
  if (archs.has(arch))
    return arch;

  if (enforceCpuSubType)
    return AK_unknown;

  // Find ABI compatible slice instead.
  return archs.getABICompatibleSlice(arch);
}

LinkerInterfaceFile::LinkerInterfaceFile() noexcept
    : _pImpl{new LinkerInterfaceFile::Impl} {}
LinkerInterfaceFile::~LinkerInterfaceFile() noexcept = default;
LinkerInterfaceFile::LinkerInterfaceFile(LinkerInterfaceFile &&) noexcept =
    default;
LinkerInterfaceFile &LinkerInterfaceFile::
operator=(LinkerInterfaceFile &&) noexcept = default;

std::vector<std::string>
LinkerInterfaceFile::getSupportedFileExtensions() noexcept {
  return {".tbd"};
}

/// \brief Load and parse the provided TBD file in the buffer and return on
///        success the interface file.
static Expected<std::unique_ptr<const InterfaceFile>>
loadFile(std::unique_ptr<MemoryBuffer> buffer,
         ReadFlags readFlags = ReadFlags::Symbols) {
  Registry registry;
  registry.addYAMLReaders();
  registry.addDiagnosticReader();

  auto textFile = registry.readFile(std::move(buffer), readFlags);
  if (!textFile)
    return textFile.takeError();

  return std::unique_ptr<const InterfaceFile>(
      cast<const InterfaceFile>(textFile.get().release()));
}

bool LinkerInterfaceFile::isSupported(const std::string &path,
                                      const uint8_t *data,
                                      size_t size) noexcept {
  Registry registry;
  registry.addYAMLReaders();
  registry.addDiagnosticReader();
  auto memBuffer = MemoryBufferRef(
      StringRef(reinterpret_cast<const char *>(data), size), path);
  return registry.canRead(memBuffer);
}

bool LinkerInterfaceFile::shouldPreferTextBasedStubFile(
    const std::string &path) noexcept {
  auto errorOr = MemoryBuffer::getFile(path);
  if (errorOr.getError())
    return false;

  auto file = loadFile(std::move(errorOr.get()), ReadFlags::Header);
  if (!file) {
    consumeError(file.takeError());
    return false;
  }

  return file.get()->isInstallAPI();
}

bool LinkerInterfaceFile::areEquivalent(const std::string &tbdPath,
                                        const std::string &dylibPath) noexcept {
  Registry registry;
  registry.addYAMLReaders();
  registry.addBinaryReaders();
  registry.addDiagnosticReader();

  auto tbdErrorOr = MemoryBuffer::getFile(tbdPath);
  if (tbdErrorOr.getError())
    return false;

  auto textFile = loadFile(std::move(tbdErrorOr.get()), ReadFlags::Header);
  if (!textFile) {
    consumeError(textFile.takeError());
    return false;
  }

  if (textFile.get()->uuids().empty())
    return false;

  auto machoErrorOr = MemoryBuffer::getFile(dylibPath);
  if (machoErrorOr.getError())
    return false;

  auto machoFile =
      registry.readFile(std::move(machoErrorOr.get()), ReadFlags::Header);
  if (!machoFile) {
    consumeError(machoFile.takeError());
    return false;
  }

  for (const auto &uuid1 : textFile.get()->uuids()) {
    // Ignore unknown architectures.
    if (uuid1.first == AK_unknown)
      continue;

    auto it = find_if(machoFile.get()->uuids(),
                      [&](const std::pair<Target, std::string> &uuid2) {
                        return uuid1.first == uuid2.first;
                      });

    if (it == machoFile.get()->uuids().end())
      continue;

    if (uuid1 != *it)
      return false;
  }
  return true;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static tapi::Platform
mapRawValuesToPlatform(const std::vector<uint32_t> &platforms) {
  Platform platform = Platform::Unknown;

  for (auto p : platforms) {
    switch (p) {
    default:
      // skip
      break;
    case MachO::PLATFORM_MACOS:
      if (platform == Platform::iOSMac)
        platform = Platform::zippered;
      else
        platform = Platform::OSX;
      break;
    case MachO::PLATFORM_IOS:
    case MachO::PLATFORM_IOSSIMULATOR:
      platform = Platform::iOS;
      break;
    case MachO::PLATFORM_MACCATALYST:
      if (platform == Platform::OSX)
        platform = Platform::zippered;
      else
        platform = Platform::iOSMac;
      break;
    case MachO::PLATFORM_WATCHOS:
    case MachO::PLATFORM_WATCHOSSIMULATOR:
      platform = Platform::watchOS;
      break;
    case MachO::PLATFORM_TVOS:
    case MachO::PLATFORM_TVOSSIMULATOR:
      platform = Platform::tvOS;
      break;
    case MachO::PLATFORM_BRIDGEOS:
      platform = Platform::bridgeOS;
      break;
    case MachO::PLATFORM_DRIVERKIT:
      platform = Platform::DriverKit;
      break;
    }
  }

  return platform;
}

static uint32_t mapPlatformToRawValue(tapi::internal::Platform platform) {
  switch (platform) {
  default:
    return 0;
  case tapi::internal::Platform::macOS:
    return MachO::PLATFORM_MACOS;
  case tapi::internal::Platform::iOS:
    return MachO::PLATFORM_IOS;
  case tapi::internal::Platform::iOSSimulator:
    return MachO::PLATFORM_IOSSIMULATOR;
  case tapi::internal::Platform::macCatalyst:
    return MachO::PLATFORM_MACCATALYST;
  case tapi::internal::Platform::watchOS:
    return MachO::PLATFORM_WATCHOS;
  case tapi::internal::Platform::watchOSSimulator:
    return MachO::PLATFORM_WATCHOSSIMULATOR;
  case tapi::internal::Platform::tvOS:
    return MachO::PLATFORM_TVOS;
  case tapi::internal::Platform::tvOSSimulator:
    return MachO::PLATFORM_TVOSSIMULATOR;
  case tapi::internal::Platform::bridgeOS:
    return MachO::PLATFORM_BRIDGEOS;
  case tapi::internal::Platform::DriverKit:
    return MachO::PLATFORM_DRIVERKIT;
  }
}
#pragma clang diagnostic pop

bool LinkerInterfaceFile::Impl::init(
    const std::shared_ptr<const InterfaceFile> &interface, cpu_type_t cpuType,
    cpu_subtype_t cpuSubType, ParsingFlags flags, PackedVersion32 minOSVersion,
    std::string &errorMessage) noexcept {
  _interface = interface;
  bool enforceCpuSubType = flags & ParsingFlags::ExactCpuSubType;
  auto arch = getArchForCPU(cpuType, cpuSubType, enforceCpuSubType,
                            interface->getArchitectures());
  if (arch == AK_unknown) {
    auto arch = getArchType(cpuType, cpuSubType);
    auto count = interface->getArchitectures().count();
    if (count > 1)
      errorMessage = "missing required architecture " +
                     getArchName(arch).str() + " in file " +
                     interface->getPath() + " (" + std::to_string(count) +
                     " slices)";
    else
      errorMessage = "missing required architecture " +
                     getArchName(arch).str() + " in file " +
                     interface->getPath();
    return false;
  }

  // Remove the patch level.
  minOSVersion =
      PackedVersion32(minOSVersion.getMajor(), minOSVersion.getMinor(), 0);

  for (auto platform : interface->getPlatforms()) {
    auto value = mapPlatformToRawValue(platform);
    if (value == 0)
      continue;
    _platforms.emplace_back(value);
  }
  llvm::sort(_platforms);
  _installName = interface->getInstallName();
  _currentVersion = interface->getCurrentVersion();
  _compatibilityVersion = interface->getCompatibilityVersion();
  _hasTwoLevelNamespace = interface->isTwoLevelNamespace();
  _isAppExtensionSafe = interface->isApplicationExtensionSafe();
  _swiftABIVersion = interface->getSwiftABIVersion();
  for (const auto &it : interface->umbrellas()) {
    if (it.first.architecture != arch)
      continue;
    _parentFrameworkName = it.second;
    break;
  }

  switch (interface->getFileType().version) {
  default:
    _fileType = FileType::Unsupported;
    break;
  case 1:
    _fileType = FileType::TBD_V1;
    break;
  case 2:
    _fileType = FileType::TBD_V2;
    break;
  case 3:
    _fileType = FileType::TBD_V3;
    break;
  }

  // Pre-scan for special linker symbols.
  for (const auto *symbol : interface->exports()) {
    if (symbol->getKind() != XPIKind::GlobalSymbol)
      continue;

    if (!symbol->hasArchitecture(arch))
      continue;

    processSymbol(symbol->getName(), minOSVersion,
                  flags & ParsingFlags::DisallowWeakImports);
  }
  sort(_ignoreExports);
  auto last = std::unique(_ignoreExports.begin(), _ignoreExports.end());
  _ignoreExports.erase(last, _ignoreExports.end());

  bool useObjC1ABI =
      interface->getPlatforms().count(tapi::internal::Platform::macOS) &&
      (arch == AK_i386);
  for (const auto *symbol : interface->exports()) {
    if (!symbol->hasArchitecture(arch))
      continue;

    switch (symbol->getKind()) {
    case XPIKind::GlobalSymbol:
      if (symbol->getName().startswith("$ld$"))
        continue;
      addSymbol(symbol->getName(), symbol->getFlags());
      break;
    case XPIKind::ObjectiveCClass:
      if (useObjC1ABI) {
        addSymbol(".objc_class_name_" + symbol->getName().str(),
                  symbol->getFlags());
      } else {
        addSymbol("_OBJC_CLASS_$_" + symbol->getName().str(),
                  symbol->getFlags());
        addSymbol("_OBJC_METACLASS_$_" + symbol->getName().str(),
                  symbol->getFlags());
      }
      break;
    case XPIKind::ObjectiveCClassEHType:
      addSymbol("_OBJC_EHTYPE_$_" + symbol->getName().str(),
                symbol->getFlags());
      break;
    case XPIKind::ObjectiveCInstanceVariable:
      addSymbol("_OBJC_IVAR_$_" + symbol->getName().str(), symbol->getFlags());
      break;
    }

    if (symbol->isWeakDefined())
      _hasWeakDefExports = true;
  }

  for (const auto *symbol : interface->undefineds()) {
    if (!symbol->hasArchitecture(arch))
      continue;

    switch (symbol->getKind()) {
    case XPIKind::GlobalSymbol:
      _undefineds.emplace_back(symbol->getName(),
                               static_cast<SymbolFlags>(symbol->getFlags()));
      break;
    case XPIKind::ObjectiveCClass:
      if (useObjC1ABI) {
        _undefineds.emplace_back(".objc_class_name_" + symbol->getName().str(),
                                 static_cast<SymbolFlags>(symbol->getFlags()));
      } else {
        _undefineds.emplace_back("_OBJC_CLASS_$_" + symbol->getName().str(),
                                 static_cast<SymbolFlags>(symbol->getFlags()));
        _undefineds.emplace_back("_OBJC_METACLASS_$_" + symbol->getName().str(),
                                 static_cast<SymbolFlags>(symbol->getFlags()));
      }
      break;
    case XPIKind::ObjectiveCClassEHType:
      _undefineds.emplace_back("_OBJC_EHTYPE_$_" + symbol->getName().str(),
                               static_cast<SymbolFlags>(symbol->getFlags()));
      break;
    case XPIKind::ObjectiveCInstanceVariable:
      _undefineds.emplace_back("_OBJC_IVAR_$_" + symbol->getName().str(),
                               static_cast<SymbolFlags>(symbol->getFlags()));
      break;
    }
  }

  for (const auto &lib : interface->allowableClients())
    for (const auto &target : lib.targets())
      if (target.architecture == arch)
        _allowableClients.emplace_back(lib.getInstallName());

  for (const auto &lib : interface->reexportedLibraries())
    for (const auto &target : lib.targets())
      if (target.architecture == arch)
        _reexportedLibraries.emplace_back(lib.getInstallName());

  for (auto &file : interface->_documents) {
    auto framework = std::static_pointer_cast<const InterfaceFile>(file);
    _inlinedFrameworkNames.emplace_back(framework->getInstallName());
    _inlinedFrameworks.emplace_back(framework);
  }

  return true;
}

LinkerInterfaceFile *LinkerInterfaceFile::create(
    const std::string &path, const uint8_t *data, size_t size,
    cpu_type_t cpuType, cpu_subtype_t cpuSubType,
    CpuSubTypeMatching matchingMode, PackedVersion32 minOSVersion,
    std::string &errorMessage) noexcept {

  ParsingFlags flags = (matchingMode == CpuSubTypeMatching::Exact)
                           ? ParsingFlags::ExactCpuSubType
                           : ParsingFlags::None;

  return create(path, data, size, cpuType, cpuSubType, flags, minOSVersion,
                errorMessage);
}

LinkerInterfaceFile *LinkerInterfaceFile::create(
    const std::string &path, const uint8_t *data, size_t size,
    cpu_type_t cpuType, cpu_subtype_t cpuSubType, ParsingFlags flags,
    PackedVersion32 minOSVersion, std::string &errorMessage) noexcept {

  if (path.empty() || data == nullptr || size < 8) {
    errorMessage = "invalid argument";
    return nullptr;
  }

  // Use a copy to make sure the buffer is null-terminated (the YAML parser
  // relies on that). Mmap guarantees that pages are padded with zeros, so
  // this mostly works, but it breaks down when a TBD file size is exactly
  // a multiple of the page size.
  // We could make the copy conditional on the file size, but as we're going
  // to read it completely anyway, I doubt there's any real performance
  // benefit to balance the added complexity.
  auto input = MemoryBuffer::getMemBufferCopy(
      StringRef(reinterpret_cast<const char *>(data), size), path);

  auto interfaceOrError = loadFile(std::move(input));
  if (!interfaceOrError) {
    errorMessage = toString(interfaceOrError.takeError());
    return nullptr;
  }

  std::shared_ptr<const InterfaceFile> interface =
      std::move(interfaceOrError.get());

  auto file = new LinkerInterfaceFile;
  if (file == nullptr) {
    errorMessage = "could not allocate memory";
    return nullptr;
  }

  if (file->_pImpl->init(interface, cpuType, cpuSubType, flags, minOSVersion,
                         errorMessage)) {
    return file;
  }

  delete file;
  return nullptr;
}

LinkerInterfaceFile *
LinkerInterfaceFile::create(const std::string &path, cpu_type_t cpuType,
                            cpu_subtype_t cpuSubType, ParsingFlags flags,
                            PackedVersion32 minOSVersion,
                            std::string &errorMessage) noexcept {

  auto errorOr = MemoryBuffer::getFile(path);
  if (auto ec = errorOr.getError()) {
    errorMessage = ec.message();
    return nullptr;
  }

  auto interfaceOrError = loadFile(std::move(errorOr.get()));
  if (!interfaceOrError) {
    errorMessage = toString(interfaceOrError.takeError());
    return nullptr;
  }

  auto file = new LinkerInterfaceFile;
  if (file == nullptr) {
    errorMessage = "could not allocate memory";
    return nullptr;
  }

  std::shared_ptr<const InterfaceFile> interface =
      std::move(interfaceOrError.get());

  if (file->_pImpl->init(interface, cpuType, cpuSubType, flags, minOSVersion,
                         errorMessage)) {
    return file;
  }

  delete file;
  return nullptr;
}

FileType LinkerInterfaceFile::getFileType() const noexcept {
  return _pImpl->_fileType;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
Platform LinkerInterfaceFile::getPlatform() const noexcept {
  return mapRawValuesToPlatform(_pImpl->_platforms);
}
#pragma clang diagnostic pop

const std::vector<uint32_t> &LinkerInterfaceFile::getPlatformSet() const
    noexcept {
  return _pImpl->_platforms;
}

const std::string &LinkerInterfaceFile::getInstallName() const noexcept {
  return _pImpl->_installName;
}

bool LinkerInterfaceFile::isInstallNameVersionSpecific() const noexcept {
  return _pImpl->_installPathOverride;
}

PackedVersion32 LinkerInterfaceFile::getCurrentVersion() const noexcept {
  return _pImpl->_currentVersion;
}

PackedVersion32 LinkerInterfaceFile::getCompatibilityVersion() const noexcept {
  return _pImpl->_compatibilityVersion;
}

unsigned LinkerInterfaceFile::getSwiftVersion() const noexcept {
  return _pImpl->_swiftABIVersion;
}

ObjCConstraint LinkerInterfaceFile::getObjCConstraint() const noexcept {
  return ObjCConstraint::None;
}

bool LinkerInterfaceFile::hasTwoLevelNamespace() const noexcept {
  return _pImpl->_hasTwoLevelNamespace;
}

bool LinkerInterfaceFile::isApplicationExtensionSafe() const noexcept {
  return _pImpl->_isAppExtensionSafe;
}

bool LinkerInterfaceFile::hasAllowableClients() const noexcept {
  return !_pImpl->_allowableClients.empty();
}

bool LinkerInterfaceFile::hasReexportedLibraries() const noexcept {
  return !_pImpl->_reexportedLibraries.empty();
}

bool LinkerInterfaceFile::hasWeakDefinedExports() const noexcept {
  return _pImpl->_hasWeakDefExports;
}

const std::string &LinkerInterfaceFile::getParentFrameworkName() const
    noexcept {
  return _pImpl->_parentFrameworkName;
}

const std::vector<std::string> &LinkerInterfaceFile::allowableClients() const
    noexcept {
  return _pImpl->_allowableClients;
}

const std::vector<std::string> &LinkerInterfaceFile::reexportedLibraries() const
    noexcept {
  return _pImpl->_reexportedLibraries;
}

const std::vector<std::string> &LinkerInterfaceFile::ignoreExports() const
    noexcept {
  return _pImpl->_ignoreExports;
}

const std::vector<Symbol> &LinkerInterfaceFile::exports() const noexcept {
  return _pImpl->_exports;
}

const std::vector<Symbol> &LinkerInterfaceFile::undefineds() const noexcept {
  return _pImpl->_undefineds;
}

const std::vector<std::string> &
LinkerInterfaceFile::inlinedFrameworkNames() const noexcept {
  return _pImpl->_inlinedFrameworkNames;
}

LinkerInterfaceFile *LinkerInterfaceFile::getInlinedFramework(
    const std::string &installName, cpu_type_t cpuType,
    cpu_subtype_t cpuSubType, ParsingFlags flags, PackedVersion32 minOSVersion,
    std::string &errorMessage) const noexcept {

  auto it = std::find_if(_pImpl->_inlinedFrameworks.begin(),
                         _pImpl->_inlinedFrameworks.end(),
                         [&](const std::shared_ptr<const InterfaceFile> &it) {
                           return it->getInstallName() == installName;
                         });

  if (it == _pImpl->_inlinedFrameworks.end()) {
    errorMessage = "no such inlined framework";
    return nullptr;
  }

  auto file = new LinkerInterfaceFile;
  if (file == nullptr) {
    errorMessage = "could not allocate memory";
    return nullptr;
  }

  if (file->_pImpl->init(*it, cpuType, cpuSubType, flags, minOSVersion,
                         errorMessage))
    return file;

  delete file;
  return nullptr;
}

TAPI_NAMESPACE_V1_END
