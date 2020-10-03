//===- ConfigurationFileReader.h - Configuration File Reader ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief The configuration file reader.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_DRIVER_CONFIGURATION_FILE_READER_H
#define TAPI_DRIVER_CONFIGURATION_FILE_READER_H

#include "tapi/Defines.h"
#include "tapi/Driver/ConfigurationFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

class ConfigurationFileReader {
  class Implementation;

  Implementation &impl;

  ConfigurationFileReader(std::unique_ptr<llvm::MemoryBuffer> inputBuffer,
                          llvm::Error &error);

public:
  static llvm::Expected<std::unique_ptr<ConfigurationFileReader>>
  get(std::unique_ptr<llvm::MemoryBuffer> inputBuffer);

  ~ConfigurationFileReader();

  ConfigurationFileReader(const ConfigurationFileReader &) = delete;
  ConfigurationFileReader &operator=(const ConfigurationFileReader &) = delete;

  ConfigurationFile takeConfigurationFile();
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_DRIVER_CONFIGURATION_FILE_READER_H
