// RUN: %tapi-frontend -target i386-apple-macos10.12 %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 %s 2>&1 | FileCheck %s

// CHECK:      - name: _globalVar1
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:10.9 o:10.11 u:0
extern int globalVar1 __attribute__((availability(
    macosx, introduced = 10.9, deprecated = 10.10, obsoleted = 10.11)));

// CHECK:      - name: _func1
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:10.8 o:10.12 u:0
void func1(void);
void func1(void)
    __attribute__((availability(macosx, introduced = 10.8, deprecated = 10.11,
                                obsoleted = 10.12)));
