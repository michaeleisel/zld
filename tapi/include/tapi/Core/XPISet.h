//===- tapi/Core/XPISet.h - TAPI XPI Set ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines the XPI Set - A set of API, SPI, etc
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_XPISET_H
#define TAPI_CORE_XPISET_H

#include "tapi/Core/Architecture.h"
#include "tapi/Core/ArchitectureSet.h"
#include "tapi/Core/AvailabilityInfo.h"
#include "tapi/Core/XPI.h"
#include "tapi/Defines.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Allocator.h"
#include <stddef.h>

namespace clang {
class PresumedLoc;
} // namespace clang

TAPI_NAMESPACE_INTERNAL_BEGIN

struct SymbolsMapKey {
  XPIKind kind;
  StringRef name;

  SymbolsMapKey(XPIKind kind, StringRef name) : kind(kind), name(name) {}
};

TAPI_NAMESPACE_INTERNAL_END

namespace llvm {
using namespace TAPI_INTERNAL;
template <> struct DenseMapInfo<SymbolsMapKey> {
  static inline SymbolsMapKey getEmptyKey() {
    return SymbolsMapKey(XPIKind::GlobalSymbol, StringRef{});
  }

  static inline SymbolsMapKey getTombstoneKey() {
    return SymbolsMapKey(XPIKind::ObjectiveCInstanceVariable, StringRef{});
  }

  static unsigned getHashValue(const SymbolsMapKey &key) {
    return combineHashValue(hash_value(key.kind), hash_value(key.name));
  }

  static bool isEqual(const SymbolsMapKey &lhs, const SymbolsMapKey &rhs) {
    return std::tie(lhs.kind, lhs.name) == std::tie(rhs.kind, rhs.name);
  }
};
} // namespace llvm

TAPI_NAMESPACE_INTERNAL_BEGIN

class XPISet {
private:
  llvm::BumpPtrAllocator allocator;

  StringRef copyString(StringRef string) {
    if (string.empty())
      return {};

    void *ptr = allocator.Allocate(string.size(), 1);
    memcpy(ptr, string.data(), string.size());
    return StringRef(reinterpret_cast<const char *>(ptr), string.size());
  }

  GlobalSymbol *addGlobalSymbolImp(StringRef name, APILinkage linkage,
                                   APIFlags flags,
                                   APIAccess access = APIAccess::Unknown);

  ObjCClass *addObjCClassImpl(StringRef name, APILinkage linkage,
                              APIAccess access = APIAccess::Unknown);

  ObjCClassEHType *
  addObjCClassEHTypeImpl(StringRef name, APILinkage linkage,
                         APIAccess access = APIAccess::Unknown);

  ObjCInstanceVariable *
  addObjCInstanceVariableImpl(StringRef name, APILinkage linkage,
                              APIAccess access = APIAccess::Unknown);

public:
  using SymbolsMapType = llvm::DenseMap<SymbolsMapKey, XPI *>;
  SymbolsMapType _symbols;

  XPISet() = default;

  GlobalSymbol *addGlobalSymbol(StringRef name, APILinkage linkage,
                                APIFlags flags, const Target &target,
                                APIAccess access = APIAccess::Unknown,
                                AvailabilityInfo info = AvailabilityInfo()) {
    auto globalSymbol = addGlobalSymbolImp(name, linkage, flags, access);
    globalSymbol->addAvailabilityInfo(target, info);
    return globalSymbol;
  }

  template <typename RangeT,
            typename ElT = typename std::remove_reference<
                decltype(*std::begin(std::declval<RangeT>()))>::type>
  GlobalSymbol *addGlobalSymbol(StringRef name, APILinkage linkage,
                                APIFlags flags, RangeT &&targets,
                                APIAccess access = APIAccess::Unknown,
                                AvailabilityInfo info = AvailabilityInfo()) {
    auto *symbol = addGlobalSymbolImp(name, linkage, flags, access);
    for (const auto &target : targets)
      symbol->addAvailabilityInfo(target, info);

    return symbol;
  }

  ObjCClass *addObjCClass(StringRef name, APILinkage linkage,
                          const Target &target,
                          APIAccess access = APIAccess::Unknown,
                          AvailabilityInfo info = AvailabilityInfo()) {
    auto *symbol = addObjCClassImpl(name, linkage, access);
    symbol->addAvailabilityInfo(target, info);
    return symbol;
  }

  template <typename RangeT,
            typename ElT = typename std::remove_reference<
                decltype(*std::begin(std::declval<RangeT>()))>::type>
  ObjCClass *addObjCClass(StringRef name, APILinkage linkage, RangeT &&targets,
                          APIAccess access = APIAccess::Unknown,
                          AvailabilityInfo info = AvailabilityInfo()) {
    auto *symbol = addObjCClassImpl(name, linkage, access);
    for (const auto &target : targets)
      symbol->addAvailabilityInfo(target, info);
    return symbol;
  }

  ObjCClassEHType *
  addObjCClassEHType(StringRef name, APILinkage linkage, const Target &target,
                     APIAccess access = APIAccess::Unknown,
                     AvailabilityInfo info = AvailabilityInfo()) {
    auto *symbol = addObjCClassEHTypeImpl(name, linkage, access);
    symbol->addAvailabilityInfo(target, info);
    return symbol;
  }

  template <typename RangeT,
            typename ElT = typename std::remove_reference<
                decltype(*std::begin(std::declval<RangeT>()))>::type>
  ObjCClassEHType *
  addObjCClassEHType(StringRef name, APILinkage linkage, RangeT &&targets,
                     APIAccess access = APIAccess::Unknown,
                     AvailabilityInfo info = AvailabilityInfo()) {
    auto *symbol = addObjCClassEHTypeImpl(name, linkage, access);
    for (const auto &target : targets)
      symbol->addAvailabilityInfo(target, info);
    return symbol;
  }

  ObjCInstanceVariable *
  addObjCInstanceVariable(StringRef name, APILinkage linkage,
                          const Target &target,
                          APIAccess access = APIAccess::Unknown,
                          AvailabilityInfo info = AvailabilityInfo()) {
    auto *symbol = addObjCInstanceVariableImpl(name, linkage, access);
    symbol->addAvailabilityInfo(target, info);
    return symbol;
  }

  template <typename RangeT,
            typename ElT = typename std::remove_reference<
                decltype(*std::begin(std::declval<RangeT>()))>::type>
  ObjCInstanceVariable *
  addObjCInstanceVariable(StringRef name, APILinkage linkage, RangeT &&targets,
                          APIAccess access = APIAccess::Unknown,
                          AvailabilityInfo info = AvailabilityInfo()) {
    auto *symbol = addObjCInstanceVariableImpl(name, linkage, access);
    for (const auto &target : targets)
      symbol->addAvailabilityInfo(target, info);
    return symbol;
  }

  const XPI *findSymbol(const XPI &) const;
  const XPI *findSymbol(XPIKind kind, StringRef name) const;
  bool removeSymbol(XPIKind, StringRef name);

  struct const_symbol_iterator
      : public llvm::iterator_adaptor_base<
            const_symbol_iterator, SymbolsMapType::const_iterator,
            std::forward_iterator_tag, const XPI *, ptrdiff_t, const XPI *,
            const XPI *> {
    const_symbol_iterator() = default;

    template <typename U>
    const_symbol_iterator(U &&u)
        : iterator_adaptor_base(std::forward<U &&>(u)) {}

    reference operator*() const { return I->second; }
    pointer operator->() const { return I->second; }
  };
  using const_symbol_range = llvm::iterator_range<const_symbol_iterator>;

  using const_filtered_symbol_iterator =
      llvm::filter_iterator<const_symbol_iterator,
                            std::function<bool(const XPI *)>>;
  using const_filtered_symbol_range =
      llvm::iterator_range<const_filtered_symbol_iterator>;

  // range that contains all symbols.
  const_symbol_range symbols() const;

  // range that contains all defined and exported symbols.
  const_filtered_symbol_range exports() const;

  // range that contains all undefined and exported symbols.
  const_filtered_symbol_range undefineds() const;

  void *Allocate(size_t Size, unsigned Align = 8) {
    return allocator.Allocate(Size, Align);
  }
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_XPISET_H
