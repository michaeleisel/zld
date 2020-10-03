// RUN: %tapi-frontend -target i386-apple-macos10.12 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi %s 2>&1 | FileCheck %s

class Bar {
public:
  // CHECK-LABEL: globals:
  // CHECK-NEXT:  - name: __ZN3Bar1yE
  // CHECK-NEXT:    loc:
  // CHECK-NEXT:    availability: i:0 o:0 u:0
  // CHECK-NEXT:    access: public
  // CHECK-NEXT:    isWeakDefined: false
  // CHECK-NOT:  - name: __ZN3Bar1xE
  static const int x = 0;
  static int y;

  inline int func1(int x) { return x + 2; }
  inline int func2(int x);
  int func3(int x);
};

int Bar::func2(int x) { return x + 3; }
inline int Bar::func3(int x) { return x + 4; }

// CHECK: - name: __ZN4Bar2C2Ev
// CHECK: - name: __ZN4Bar2C1Ev
class Bar2 {
  Bar2();
};
Bar2::Bar2() {}
