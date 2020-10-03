//===- lib/Core/FileSystem.cpp - File System --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Implements the additional file system support.
///
//===----------------------------------------------------------------------===//

#include "tapi/Core/FileSystem.h"
#include "tapi/Core/LLVM.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

using namespace llvm;

TAPI_NAMESPACE_INTERNAL_BEGIN

std::error_code realpath(SmallVectorImpl<char> &path) {
  if (path.back() != '\0')
    path.append({'\0'});
  SmallString<PATH_MAX> result;

  errno = 0;
  const char *ptr = nullptr;
  if ((ptr = ::realpath(path.data(), result.data())) == nullptr)
    return {errno, std::generic_category()};

  assert(ptr == result.data() && "Unexpected pointer");
  result.set_size(strlen(result.data()));
  path.swap(result);
  return {};
}

std::error_code read_link(const Twine &path, SmallVectorImpl<char> &linkPath) {
  errno = 0;
  SmallString<PATH_MAX> pathStorage;
  auto p = path.toNullTerminatedStringRef(pathStorage);
  SmallString<PATH_MAX> result;
  ssize_t len;
  if ((len = ::readlink(p.data(), result.data(), PATH_MAX)) == -1)
    return {errno, std::generic_category()};

  result.set_size(len);
  linkPath.swap(result);

  return {};
}

std::error_code shouldSkipSymlink(const Twine &path, bool &result) {
  result = false;
  SmallString<PATH_MAX> pathStorage;
  auto p = path.toNullTerminatedStringRef(pathStorage);
  sys::fs::file_status stat1;
  auto ec = sys::fs::status(p.data(), stat1);
  if (ec == std::errc::too_many_symbolic_link_levels) {
    result = true;
    return {};
  }

  if (ec)
    return ec;

  StringRef parent = sys::path::parent_path(p);
  while (!parent.empty()) {
    sys::fs::file_status stat2;
    if (auto ec = sys::fs::status(parent, stat2))
      return ec;

    if (sys::fs::equivalent(stat1, stat2)) {
      result = true;
      return {};
    }

    parent = sys::path::parent_path(parent);
  }

  return {};
}

std::error_code make_relative(StringRef from, StringRef to,
                              SmallVectorImpl<char> &relativePath) {
  SmallString<PATH_MAX> src = from;
  SmallString<PATH_MAX> dst = to;
  if (auto ec = sys::fs::make_absolute(src))
    return ec;

  if (auto ec = sys::fs::make_absolute(dst))
    return ec;

  SmallString<PATH_MAX> result;
  src = sys::path::parent_path(src);
  auto it1 = sys::path::begin(src), it2 = sys::path::begin(dst),
       ie1 = sys::path::end(src), ie2 = sys::path::end(dst);
  // ignore the common part.
  for (; it1 != ie1 && it2 != ie2; ++it1, ++it2) {
    if (*it1 != *it2)
      break;
  }

  for (; it1 != ie1; ++it1)
    sys::path::append(result, "../");

  for (; it2 != ie2; ++it2)
    sys::path::append(result, *it2);

  if (result.empty())
    result = ".";

  relativePath.swap(result);

  return {};
}

TAPI_NAMESPACE_INTERNAL_END
