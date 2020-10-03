// RUN: %tapi-frontend -target i386-apple-macos10.12 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi %s 2>&1 | FileCheck %s
// XFAIL: *

// Abstract base class with a sub class. There should be weak-def RTTI for the
// abstract base class.
// The sub-class should have vtable and RTTI.
namespace test4 {
// CHECK: __ZTIN5test44BaseE
// CHECK: isWeakDefined: true
// CHECK: __ZTSN5test44BaseE
// CHECK: isWeakDefined: true
class Base {
public:
  virtual ~Base() {}
  virtual void run() = 0;
};
// CHECK: __ZTVN5test43SubE
// CHECK: isWeakDefined: false
// CHECK: __ZTIN5test43SubE
// CHECK: isWeakDefined: false
// CHECK: __ZTSN5test43SubE
// CHECK: isWeakDefined: false
class Sub : public Base {
public:
  void run() override;
};
} // end namespace test4
