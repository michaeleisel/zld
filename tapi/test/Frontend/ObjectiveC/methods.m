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
@interface Foo
@property int property1;

// CHECK-NEXT:   methods:
// CHECK-NEXT:   - name: aClassMethod:to:
// CHECK-NEXT:     kind: class
// CHECK-NEXT:     isOptional: false
// CHECK-NEXT:     isDynamic: false
// CHECK-NEXT:     loc:
// CHECK-NEXT:     availability: i:0 o:0 u:0
+ (void)aClassMethod:(int)x to:(int)y;
// CHECK-NEXT:   - name: anInstanceMethod
// CHECK-NEXT:     kind: instance
// CHECK-NEXT:     isOptional: false
// CHECK-NEXT:     isDynamic: false
// CHECK-NEXT:     loc:
// CHECK-NEXT:     availability: i:0 o:0 u:0
- (void)anInstanceMethod;
@end
