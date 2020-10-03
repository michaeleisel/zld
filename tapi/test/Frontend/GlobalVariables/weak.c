// RUN: %tapi-frontend -target i386-apple-macos10.12 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi %s 2>&1 | FileCheck %s

// CHECK-LABEL: globals:
// CHECK-NEXT:  - name: _var1
// CHECK-NEXT:    loc:
// CHECK-NEXT:    availability: i:0 o:0 u:0
// CHECK-NEXT:    access: public
// CHECK-NEXT:    isWeakDefined: true
int var1 __attribute__((weak));
