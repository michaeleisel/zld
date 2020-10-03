// RUN: %tapi-frontend -target i386-apple-macos10.12 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi %s 2>&1 | FileCheck %s

// CHECK:      - name: _globalVar1
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:0 o:0 u:1
extern int globalVar1 __attribute__((availability(macosx, unavailable)))
__attribute__((availability(ios, unavailable)));

// CHECK:      - name: _func1
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:0 o:0 u:1
void func1(void);
void func1(void) __attribute__((unavailable));
