// RUN: %tapi-frontend -target i386-apple-macos10.15 %s 2>&1 | FileCheck -check-prefixes=CHECK,CHECK_MACOS %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck -check-prefixes=CHECK,CHECK_MACOS %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi %s 2>&1 | FileCheck -check-prefixes=CHECK,CHECK_IOS %s

// CHECK:            - name: _globalVar1
// CHECK-NEXT:         loc:
// CHECK_MACOS-NEXT:   availability: i:10.9 o:0 u:0
// CHECK_IOS-NEXT:     availability: i:13 o:0 u:0
extern int globalVar1 __attribute__((availability(macosx, introduced = 10.9)));

// CHECK:            - name: _func1
// CHECK-NEXT:         loc:
// CHECK_MACOS-NEXT:   availability: i:10.10 o:0 u:0
// CHECK_IOS-NEXT:     availability: i:14 o:0 u:0
void func1(void);
void func1(void) __attribute__((availability(macosx, introduced = 10.10)))
__attribute__((availability(ios, introduced = 14.0)));
