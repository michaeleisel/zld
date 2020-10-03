// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s

@interface Foo
// CHECK-NOT: sameProp
@property int sameProp;
@end

// CHECK: property.m:[[@LINE+1]]:12: warning: 'Test' has incompatible definitions
@interface Test
#if !__is_target_environment(macabi)
// CHECK: property.m:[[@LINE+1]]:11: note: property 'a' has type 'int' here
@property int a;
#else
// CHECK: property.m:[[@LINE+1]]:11: note: property 'a' has type 'float' here
@property float a;
#endif
@end

// CHECK: property.m:[[@LINE+1]]:12: warning: 'Test1' has incompatible definitions
@interface Test1
#if !__is_target_environment(macabi)
// CHECK: property.m:[[@LINE+1]]:10: note: property 'r' has attribute here
@property(readonly) int r;
#else
// CHECK: property.m:[[@LINE+1]]:10: note: property 'r' has attribute here
@property(readwrite) int r;
#endif
@end

// CHECK: property.m:[[@LINE+1]]:12: warning: 'Test2' has incompatible definitions
@interface Test2
#if !__is_target_environment(macabi)
// CHECK: property.m:[[@LINE+1]]:10: note: property 'r2' has attribute here
@property(readonly) int r2;
#else
// CHECK: property.m:[[@LINE+1]]:1: note: property 'r2' has attribute here
@property int r2;
#endif
@end

// CHECK-NOT: 'Test3' has incompatible definitions
@interface Test3
#if !__is_target_environment(macabi)
@property(readwrite) int r3;
#else
@property int r3;
#endif
@end

// CHECK: property.m:[[@LINE+1]]:12: warning: 'Test4' has incompatible definitions
@interface Test4
#if !__is_target_environment(macabi)
// CHECK: property.m:[[@LINE+1]]:10: note: property 'GS' has attribute here
@property(getter=isGS, setter=setGS:) int GS;
#else
// CHECK: property.m:[[@LINE+1]]:1: note: property 'GS' has attribute here
@property int GS;
#endif
@end

// CHECK-NOT: warning: 'Test5' has incompatible definitions
@interface Test5
#if !__is_target_environment(macabi)
@property int b __attribute__((availability(maccatalyst, unavailable)));
#else
@property int c __attribute__((availability(maccatalyst, introduced=1.0)));
#endif
@end

// CHECK-NOT: warning: 'Test6' has incompatible definitions
@interface Test6
#if __is_target_environment(macabi)
@property(readonly, assign) int r;
@property(nonatomic) int a;
#else
@property(readwrite, assign) int r;
@property int a; // default atomic
#endif
@end

// CHECK-NOT: warning: 'Test7' has incompatible definitions
@interface Test7
@property(class) int p;
@property int p;
@end
