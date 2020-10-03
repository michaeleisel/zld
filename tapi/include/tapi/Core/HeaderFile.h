//===- include/Core/HeaderFile.h - Header File ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Header file enums and defines.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_HEADER_FILE_H
#define TAPI_CORE_HEADER_FILE_H

#include "tapi/Core/LLVM.h"
#include "tapi/Defines.h"
#include "llvm/ADT/StringRef.h"
#include <string>

TAPI_NAMESPACE_INTERNAL_BEGIN

enum class HeaderType {
  Public,
  Private,
  Project,
};

struct HeaderFile {
  std::string fullPath;
  std::string relativePath;
  std::string includeName;
  HeaderType type;
  bool isUmbrellaHeader{false};
  bool isExcluded{false};
  bool isExtra{false};
  bool isPreInclude{false};

  HeaderFile(StringRef fullPath, HeaderType type,
             StringRef relativePath = StringRef())
      : fullPath(fullPath), relativePath(relativePath), type(type) {}

  void print(raw_ostream &os) const;
  friend bool operator<(const HeaderFile &lhs, const HeaderFile &rhs);
};

// Sort by type first.
inline bool operator<(const HeaderFile &lhs, const HeaderFile &rhs) {
  if (lhs.isExtra && rhs.isExtra) {
    return std::tie(lhs.type, rhs.isUmbrellaHeader, lhs.isExtra) <
           std::tie(rhs.type, lhs.isUmbrellaHeader, rhs.isExtra);
  }

  return std::tie(lhs.type, rhs.isUmbrellaHeader, lhs.isExtra, lhs.fullPath) <
         std::tie(rhs.type, lhs.isUmbrellaHeader, rhs.isExtra, rhs.fullPath);
}

inline raw_ostream &operator<<(raw_ostream &os, const HeaderFile &file) {
  file.print(os);
  return os;
}

using HeaderSeq = std::vector<HeaderFile>;

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_HEADER_FILE_H
