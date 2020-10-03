// RUN: %tapi-frontend -target i386-apple-macos10.12 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi %s 2>&1 | FileCheck %s
// XFAIL: *

// Abstract base class with a sub class. Same as test4, but with a user defined
// inlined destructor.
namespace test5 {
// CHECK: __ZTIN5test54BaseE
// CHECK: isWeakDefined: true
// CHECK: __ZTSN5test54BaseE
// CHECK: isWeakDefined: true
class Base {
public:
  virtual ~Base() {}
  virtual void run() = 0;
};
// CHECK: __ZTVN5test53SubE
// CHECK: isWeakDefined: false
// CHECK: __ZTIN5test53SubE
// CHECK: isWeakDefined: false
// CHECK: __ZTSN5test53SubE
// CHECK: isWeakDefined: false
class Sub : public Base {
public:
  virtual ~Sub() {}
  void run() override;
};
} // end namespace test5
