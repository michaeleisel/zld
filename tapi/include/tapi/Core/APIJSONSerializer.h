//===- tapi/Core/APIJSONSerializer.h - TAPI API JSON Serializer -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Declares the TAPI JSON Serializer
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_APIJSONSERIALIZER_H
#define TAPI_CORE_APIJSONSERIALIZER_H

#include "tapi/Core/APIVisitor.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

TAPI_NAMESPACE_INTERNAL_BEGIN

struct APIJSONOption {
  bool compact;
  bool noUUID;
  bool noTarget;
  bool externalOnly;
  bool publicOnly;
};

class APIJSONSerializer {
public:
  APIJSONSerializer(const API &api, APIJSONOption options = APIJSONOption())
      : api(api), options(options) {}
  ~APIJSONSerializer() {}

  llvm::json::Object getJSONObject() const;

  void serialize(raw_ostream &os) const;

  // static method to parse JSON into API.
  static llvm::Expected<API> parse(StringRef JSON);
  static llvm::Expected<API> parse(llvm::json::Object *root,
                                   bool publicOnly = false,
                                   llvm::Triple *triple = nullptr);

private:
  const API &api;
  APIJSONOption options;
};

class APIJSONError : public llvm::ErrorInfo<llvm::json::ParseError> {
public:
  APIJSONError(Twine ErrorMsg) : Msg(ErrorMsg.str()) {}

  void log(llvm::raw_ostream &os) const override {
    os << Msg << "\n";
  }
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }
private:
  std::string Msg;
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_APIJSONSERIALIZER_H
