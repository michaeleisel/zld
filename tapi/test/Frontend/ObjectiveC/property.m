// RUN: %tapi-frontend -target i386-apple-macos10.12 -isysroot %sysroot %s 2>&1 | FileCheck -check-prefixes=CHECK,CHECK_I386 %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -isysroot %sysroot %s 2>&1 | FileCheck -check-prefixes=CHECK,CHECK_X86_64 %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi -isysroot %sysroot %s 2>&1 | FileCheck -check-prefixes=CHECK,CHECK_X86_64 %s

// CHECK-LABEL: objective-c interfaces:

// CHECK:      - name: Foo
// CHECK-NEXT:   superClassName:
// CHECK-NEXT:   hasExceptionAttribute: false
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:0 o:0 u:0
// CHECK-NEXT:   categories:
// CHECK-NEXT:   protocols:
// CHECK-NEXT:   methods:
@interface Foo
// CHECK-NEXT:   properties:
// CHECK-NEXT:   - name: a
// CHECK-NEXT:     attributes:
// CHECK-NEXT:     isOptional: false
// CHECK-NEXT:     getter name: a
// CHECK-NEXT:     setter name: setA
// CHECK-NEXT:     loc:
// CHECK-NEXT:     availability: i:0 o:0 u:0
@property int a;
// CHECK-NEXT:   - name: finished
// CHECK-NEXT:     attributes:
// CHECK-NEXT:     isOptional: false
// CHECK-NEXT:     getter name: isFinished
// CHECK-NEXT:     setter name: markFinished:
// CHECK-NEXT:     loc:
// CHECK-NEXT:     availability: i:0 o:0 u:0
@property(getter=isFinished, setter=markFinished:) int finished;
// CHECK-NEXT:   - name: b
// CHECK-NEXT:     attributes: readonly
// CHECK-NEXT:     isOptional: false
// CHECK-NEXT:     getter name: b
// CHECK-NEXT:     loc:
// CHECK-NEXT:     availability: i:0 o:0 u:0
@property(readonly) int b;
@end
