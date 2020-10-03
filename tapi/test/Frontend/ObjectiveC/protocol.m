// RUN: %tapi-frontend -target i386-apple-macos10.12 -isysroot %sysroot %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -isysroot %sysroot %s 2>&1 | FileCheck %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi -isysroot %sysroot %s 2>&1 | FileCheck %s

// CHECK-LABEL: objective-c protocols:
// CHECK-NEXT:  - name: Bar
// CHECK-NEXT:    loc:
// CHECK-NEXT:    availability: i:0 o:0 u:0
// CHECK-NEXT:    protocols:
// CHECK-NEXT:    methods:
// CHECK-NEXT:    - name: bar
// CHECK-NEXT:      kind: instance
// CHECK-NEXT:      isOptional: true
// CHECK-NEXT:      isDynamic: false
// CHECK-NEXT:      loc:
// CHECK-NEXT:      availability: i:0 o:0 u:0
// CHECK-NEXT:    properties:
@protocol Bar
@optional
- (void)bar;
@end

// CHECK-NEXT:  - name: Baz
// CHECK-NEXT:    loc:
// CHECK-NEXT:    availability: i:0 o:0 u:0
// CHECK-NEXT:    protocols: Bar
// CHECK-NEXT:    methods:
// CHECK-NEXT:    properties:
// CHECK-NEXT:    - name: baz
// CHECK-NEXT:      attributes:
// CHECK-NEXT:      isOptional: true
// CHECK-NEXT:      getter name: baz
// CHECK-NEXT:      setter name: setBaz:
// CHECK-NEXT:      loc:
// CHECK-NEXT:      availability: i:0 o:0 u:0
@protocol Baz <Bar>
@optional
@property int baz;
@end

// CHECK-LABEL: objective-c interfaces:
// CHECK-NEXT:  - name: Foo
// CHECK-NEXT:    superClassName:
// CHECK-NEXT:    hasExceptionAttribute: false
// CHECK-NEXT:    loc:
// CHECK-NEXT:    availability: i:0 o:0 u:0
// CHECK-NEXT:    categories: NewStuff
// CHECK-NEXT:    protocols: Bar
@interface Foo <Bar>
@end

// CHECK-LABEL: objective-c categories:
// CHECK-NEXT:  - name: NewStuff
// CHECK-NEXT:    interfaceName: Foo
// CHECK-NEXT:    loc:
// CHECK-NEXT:    availability: i:0 o:0 u:0
// CHECK-NEXT:    protocols: Baz
@interface Foo (NewStuff) <Baz>
@end
