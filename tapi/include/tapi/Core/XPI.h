//===- tapi/Core/XPI.h - TAPI XPI -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Defines XPI - API, SPI, etc
///
//===----------------------------------------------------------------------===//

#ifndef TAPI_CORE_XPI_H
#define TAPI_CORE_XPI_H

#include "tapi/Core/APICommon.h"
#include "tapi/Core/Architecture.h"
#include "tapi/Core/ArchitectureSet.h"
#include "tapi/Core/AvailabilityInfo.h"
#include "tapi/Core/LLVM.h"
#include "tapi/Core/Target.h"
#include "tapi/Defines.h"
#include "tapi/Symbol.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Allocator.h"
#include <map>
#include <utility>

TAPI_NAMESPACE_INTERNAL_BEGIN

class XPISet;

/// \brief The different XPI kinds.
enum class XPIKind : unsigned {
  GlobalSymbol,
  ObjectiveCClass,
  ObjectiveCClassEHType,
  ObjectiveCInstanceVariable,
};

class XPI {
protected:
  /// \brief Construct an XPI - the constructor should only be called by a
  /// sub-class.
  XPI(XPIKind kind, StringRef name, APILinkage linkage, APIFlags flags,
      APIAccess access)
      : _name(name), _kind(kind), _linkage(linkage), _flags(flags),
        _access(access) {}

public:
  XPIKind getKind() const { return _kind; }
  StringRef getName() const { return _name; }
  APILinkage getLinkage() const { return _linkage; }
  APIFlags getFlags() const { return _flags; }
  APIAccess getAccess() const { return _access; }

  bool isWeakDefined() const {
    return (_flags & APIFlags::WeakDefined) == APIFlags::WeakDefined;
  }

  bool isWeakReferenced() const {
    return (_flags & APIFlags::WeakReferenced) == APIFlags::WeakReferenced;
  }

  bool isThreadLocalValue() const {
    return (_flags & APIFlags::ThreadLocalValue) == APIFlags::ThreadLocalValue;
  }

  bool isUndefined() const { return _linkage == APILinkage::External; }

  bool isReexported() const { return _linkage == APILinkage::Reexported; }

  void setAccess(APIAccess access) { _access = access; }
  bool updateAccess(APIAccess access) {
    if (access == APIAccess::Unknown)
      return true;

    if (getAccess() == APIAccess::Unknown) {
      setAccess(access);
      return true;
    }

    // APIAccess Public and Private are for header declaration only.
    // It is fine to re-declare the public XPI in the private header again and
    // the final APIAccess type should be public.
    if (getAccess() == APIAccess::Public && access == APIAccess::Private)
      return true;
    if (getAccess() == APIAccess::Private && access == APIAccess::Public) {
      setAccess(access);
      return true;
    }

    return getAccess() == access;
  }

  void addAvailabilityInfo(const Target &target, const AvailabilityInfo &info) {
    auto it = find_if(_availability,
                      [&](const std::pair<Target, AvailabilityInfo> &avail) {
                        return target == avail.first;
                      });
    if (it != _availability.end()) {
      if (!info._unavailable && info._obsoleted.empty()) {
        it->second._unavailable = false;
      }
      return;
    }

    _availability.emplace_back(target, info);
  }

  const llvm::SmallVectorImpl<std::pair<Target, AvailabilityInfo>> &
  getAvailabilityInfo() const {
    return _availability;
  }

  llvm::Optional<AvailabilityInfo>
  getAvailabilityInfo(const Target &target) const {
    auto it = find_if(_availability,
                      [&](const std::pair<Target, AvailabilityInfo> &avail) {
                        return target == avail.first;
                      });
    if (it != _availability.end())
      return it->second;

    return llvm::None;
  }

  ArchitectureSet getArchitectures() const {
    ArchitectureSet result;
    for (const auto &avail : _availability) {
      if (avail.second.isUnavailable() || !avail.second._obsoleted.empty())
        continue;

      result.set(avail.first.architecture);
    }
    return result;
  }

  bool hasArchitecture(Architecture arch) const {
    for (const auto &avail : _availability) {
      if (avail.first.architecture != arch)
        continue;

      if (avail.second.isUnavailable() || !avail.second._obsoleted.empty())
        continue;

      return true;
    }
    return false;
  }

  // The symbols is available if any target is not unavailable or obsolete.
  bool isAvailable() const {
    for (const auto &avail : _availability) {
      if (!avail.second.isUnavailable() && avail.second._obsoleted.empty())
        return true;
    }
    return false;
  }
  bool isUnavailable() const { return !isAvailable(); }
  bool isObsolete() const {
    // return true if API has been available then obsoleted.
    bool hasObsolete = false;
    for (const auto &avail : _availability) {
      // skip APIs that are unavailable.
      if (avail.second._unavailable)
        continue;
      if (avail.second._obsoleted.empty())
        return false;
      else
        hasObsolete = true;
    }
    return hasObsolete;
  }
  std::string getPrettyName(bool demangle = false) const;
  std::string getAnnotatedName(bool demangle = false) const;
  void print(raw_ostream &os) const;

  bool operator<(const XPI &other) const {
    return std::tie(_kind, _name) < std::tie(other._kind, other._name);
  }

  struct const_target_iterator
      : public llvm::iterator_adaptor_base<
            const_target_iterator,
            llvm::SmallVectorImpl<
                std::pair<Target, AvailabilityInfo>>::const_iterator,
            std::forward_iterator_tag, const Target> {
    const_target_iterator() = default;

    template <typename U>
    const_target_iterator(U &&u)
        : iterator_adaptor_base(std::forward<U &&>(u)) {}

    reference operator*() const { return I->first; }
  };
  using const_target_range = llvm::iterator_range<const_target_iterator>;
  const_target_range targets() const;

  using const_filtered_target_iterator =
      llvm::filter_iterator<const_target_iterator,
                            std::function<bool(const Target &)>>;
  using const_filtered_target_range =
      llvm::iterator_range<const_filtered_target_iterator>;
  const_filtered_target_range targets(ArchitectureSet architectures) const;

private:
  llvm::SmallVector<std::pair<Target, AvailabilityInfo>, 4> _availability;
  StringRef _name;

protected:
  /// \brief The kind of xpi.
  XPIKind _kind;

  APILinkage _linkage;

  /// \brief Hoisted GlobalSymbol flags.
  APIFlags _flags;

  /// \brief The access permission/visibility of this xpi.
  APIAccess _access;
};

inline raw_ostream &operator<<(raw_ostream &os, const XPI &xpi) {
  xpi.print(os);
  return os;
}

class GlobalSymbol : public XPI {
private:
  GlobalSymbol(StringRef name, APILinkage linkage, APIFlags flags,
               APIAccess access)
      : XPI(XPIKind::GlobalSymbol, name, linkage, flags, access) {}

public:
  static GlobalSymbol *create(llvm::BumpPtrAllocator &A, StringRef name,
                              APILinkage linkage, APIFlags flags,
                              APIAccess access);

  static bool classof(const XPI *xpi) {
    return xpi->getKind() == XPIKind::GlobalSymbol;
  }
};

class ObjCClassEHType : public XPI {
private:
  ObjCClassEHType(StringRef name, APILinkage linkage, APIAccess access)
      : XPI(XPIKind::ObjectiveCClassEHType, name, linkage, APIFlags::None,
            access) {}

public:
  static ObjCClassEHType *create(llvm::BumpPtrAllocator &A, StringRef name,
                                 APILinkage linkage, APIAccess access);

  static bool classof(const XPI *xpi) {
    return xpi->getKind() == XPIKind::ObjectiveCClassEHType;
  }
};

class ObjCInstanceVariable : public XPI {
private:
  ObjCInstanceVariable(StringRef name, APILinkage linkage, APIAccess access)
      : XPI(XPIKind::ObjectiveCInstanceVariable, name, linkage, APIFlags::None,
            access) {}

public:
  static ObjCInstanceVariable *create(llvm::BumpPtrAllocator &A, StringRef name,
                                      APILinkage linkage, APIAccess access);

  static bool classof(const XPI *xpi) {
    return xpi->getKind() == XPIKind::ObjectiveCInstanceVariable;
  }
};

class ObjCClass : public XPI {
private:
  ObjCClass(StringRef name, APILinkage linkage, APIAccess access)
      : XPI(XPIKind::ObjectiveCClass, name, linkage, APIFlags::None, access) {}

public:
  static ObjCClass *create(llvm::BumpPtrAllocator &A, StringRef name,
                           APILinkage linkage, APIAccess access);

  static bool classof(const XPI *xpi) {
    return xpi->getKind() == XPIKind::ObjectiveCClass;
  }
};

TAPI_NAMESPACE_INTERNAL_END

#endif // TAPI_CORE_XPI_H
