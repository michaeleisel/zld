// RUN: %tapi-frontend -target i386-apple-macos10.12 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi %s 2>&1 | FileCheck %s

// CHECK-LABEL: globals:

// Simple test class with virtual function. There should be an external vtable
// and RTTI.
namespace test2 {
// CHECK: __ZTVN5test26SimpleE
// CHECK: isWeakDefined: false
// CHECK: __ZTIN5test26SimpleE
// CHECK: isWeakDefined: false
// CHECK: __ZTSN5test26SimpleE
// CHECK: isWeakDefined: false
class Simple {
public:
  virtual void run();
};
} // end namespace test2
