// RUN: %tapi-frontend -target i386-apple-macos10.12 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi %s 2>&1 | FileCheck %s

// Simple test class with no virtual functions. There should be no vtable or
// RTTI.
namespace test1 {
// CHECK-NOT: __ZTVN5test16SimpleE
// CHECK-NOT: __ZTIN5test16SimpleE
// CHECK-NOT: __ZTSN5test16SimpleE
class Simple {
public:
  void run();
};
} // end namespace test1

void test1::Simple::run() {}
