// RUN: %tapi-frontend -target i386-apple-macos10.12 -isysroot %sysroot %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -isysroot %sysroot %s 2>&1 | FileCheck %s

// CHECK:      objective-c interfaces
// CHECK-NEXT: - name: Foo
// CHECK-NEXT:   superClassName:
// CHECK-NEXT:   hasExceptionAttribute: false
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:0 o:0 u:0
// CHECK-NEXT:   categories: deprecated
// CHECK-NEXT:   protocols:
// CHECK-NEXT:   methods:
// CHECK-NEXT:   properties:
// CHECK-NEXT:   - name: aProperty
// CHECK-NEXT:     attributes: readonly
// CHECK-NEXT:     isOptional: false
// CHECK-NEXT:     getter name: aProperty
// CHECK-NEXT:     loc:
// CHECK-NEXT:     availability: i:10.10 o:0 u:0
// CHECK-NEXT:   instance variables:
@interface Foo
@property(readonly) int aProperty __attribute__((availability(macosx, introduced = 10.10)));
@end

// CHECK:      objective-c categories:
// CHECK-NEXT: - name: deprecated
// CHECK-NEXT:   interfaceName: Foo
// CHECK-NEXT:   loc:
// CHECK-NEXT:   availability: i:0 o:0 u:0
// CHECK-NEXT:   protocols:
// CHECK-NEXT:   methods:
// CHECK-NEXT:   properties:
// CHECK-NEXT:   - name: aProperty
// CHECK-NEXT:     attributes:
// CHECK-NEXT:     isOptional: false
// CHECK-NEXT:     getter name: aProperty
// CHECK-NEXT:     setter name: setAProperty:
// CHECK-NEXT:     loc:
// CHECK-NEXT:     availability: i:0 o:0 u:0
// CHECK-NEXT:   instance variables:
@interface Foo (deprecated)
@property(readwrite) int aProperty __attribute__((availability(macosx, deprecated = 10.10)));
@end
