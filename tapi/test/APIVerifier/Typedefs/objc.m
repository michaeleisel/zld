// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s

#if !__is_target_environment(macabi)
@interface MacClass @end;
// CHECK: objc.m:[[@LINE+2]]:18: warning: 'MyClass' has incompatible definitions
// CHECK: objc.m:[[@LINE+1]]:9: note: 'MyClass' is defined to type 'MacClass' here
typedef MacClass MyClass;
#else
@interface iOSClass @end;
// CHECK: objc.m:[[@LINE+1]]:9: note: 'MyClass' is defined to type 'iOSClass' here
typedef iOSClass MyClass;
#endif

@protocol P - (void) foo; @end
@protocol newP <P> @end

#if !__is_target_environment(macabi)
@interface MacClass(Ext)  - (void) foo; @end
#else
@interface iOSClass(Ext) - (void) foo; @end
#endif

// CHECK: objc.m:[[@LINE+3]]:21: warning: 'MyProtoClass' has incompatible definitions
// CHECK: objc.m:[[@LINE+2]]:9: note: 'MyProtoClass' is defined to type 'MyClass<P>' (aka 'MacClass<P>') here
// CHECK: objc.m:[[@LINE+1]]:9: note: 'MyProtoClass' is defined to type 'MyClass<P>' (aka 'iOSClass<P>') here
typedef MyClass <P> MyProtoClass;
