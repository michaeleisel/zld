//===- tapi/Core/APICommon.h - TAPI API Common Types ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines common API related types.
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_API_COMMON_H
#define TAPI_CORE_API_COMMON_H

#include "tapi/Defines.h"
#include "llvm/ADT/BitmaskEnum.h"
#include <cstdint>

TAPI_NAMESPACE_INTERNAL_BEGIN

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

// clang-format off
enum class APIAccess : uint8_t {
  Unknown = 0, // Unknown, example, from binary.
  Project = 1, // APIs available in project headers.
  Private = 2, // APIs available in private headers.
  Public  = 3, // APIs available in public headers.
};

enum class APILinkage : uint8_t {
  Unknown    = 0, // Unknown.
  Internal   = 1, // API is internal.
  External   = 2, // External interface used.
  Reexported = 3, // API is re-exported.
  Exported   = 4, // API is exported.
};

enum class APIFlags : uint8_t {
  None             = 0,
  ThreadLocalValue = 1U << 0,
  WeakDefined      = 1U << 1,
  WeakReferenced   = 1U << 2,
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/WeakReferenced)
};
// clang-format on

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_API_COMMON_H
