// RUN: %tapi-frontend -target i386-apple-macos10.12 -isysroot %sysroot %s 2>&1 | FileCheck -check-prefixes=CHECK,CHECK_I386 %s
// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -isysroot %sysroot %s 2>&1 | FileCheck -check-prefixes=CHECK,CHECK_X86_64 %s
// RUN: %tapi-frontend -target x86_64-apple-ios13.0-macabi -isysroot %sysroot %s 2>&1 | FileCheck -check-prefixes=CHECK,CHECK_X86_64 %s

// CHECK-LABEL: objective-c interfaces:

@interface Foo
// CHECK:      - name: Foo
// CHECK-NEXT:   superClassName:
// CHECK-NEXT:   hasExceptionAttribute: false
@end

@interface Bar : Foo
// CHECK:      - name: Bar
// CHECK-NEXT:   superClassName: Foo
// CHECK-NEXT:   hasExceptionAttribute: false
@end

__attribute__((objc_exception))
@interface Baz : Foo
// CHECK:      - name: Baz
// CHECK-NEXT:   superClassName: Foo
// CHECK_I386-NEXT:   hasExceptionAttribute: false
// CHECK_X86_64-NEXT: hasExceptionAttribute: true
@end

@interface Fizz : Baz
// CHECK:      - name: Fizz
// CHECK-NEXT:   superClassName: Baz
// CHECK_I386-NEXT:   hasExceptionAttribute: false
// CHECK_X86_64-NEXT: hasExceptionAttribute: true
@end

__attribute__((objc_runtime_name("MindTrick")))
@interface NotTheClassYouAreLookingFor
// CHECK:      - name: MindTrick
// CHECK-NEXT:   superClassName:
// CHECK-NEXT:   hasExceptionAttribute: false
@end

__attribute__((visibility("hidden")))
@interface IAmHidden
// CHECK:      - name: IAmHidden
// CHECK:        linkage: internal
@end
