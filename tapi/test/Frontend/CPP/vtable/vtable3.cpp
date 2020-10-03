// RUN: %tapi-frontend -target i386-apple-macos10.12 %s 2>&1 | FileCheck --allow-empty %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck --allow-empty %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi %s 2>&1 | FileCheck --allow-empty %s

// Abstract class with no sub classes. There should be no vtable or RTTI.
namespace test3 {
// CHECK-NOT: __ZTVN5test38AbstractE
// CHECK-NOT: __ZTIN5test38AbstractE
// CHECK-NOT: __ZTSN5test38AbstractE
class Abstract {
public:
  virtual ~Abstract() {}
  virtual void run() = 0;
};
} // end namespace test3
