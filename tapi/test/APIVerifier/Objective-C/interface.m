// RUN: %tapi-frontend -target x86_64-apple-macos10.15 -target x86_64-apple-ios13.0-macabi -verify -no-print %s 2>&1 | FileCheck %s

@interface Base1 @end;
@interface Base2 @end;

@protocol P1 @end
@protocol P2 @end

#if !__is_target_environment(macabi)
// CHECK: interface.m:[[@LINE+2]]:12: warning: 'A' has incompatible definitions
// CHECK: interface.m:[[@LINE+1]]:16: note: interface 'A' has super class 'Base1' here
@interface A : Base1 @end;
#else
// CHECK: interface.m:[[@LINE+1]]:16: note: interface 'A' has super class 'Base2' here
@interface A : Base2 @end;
#endif

#if !__is_target_environment(macabi)
// CHECK: interface.m:[[@LINE+2]]:12: warning: 'A1' has incompatible definitions
// CHECK: interface.m:[[@LINE+1]]:17: note: interface 'A1' has super class 'Base1' here
@interface A1 : Base1 @end;
#else
// CHECK: interface.m:[[@LINE+1]]:12: note: interface 'A1' has no corresponding super class here
@interface A1 @end;
#endif

#if !__is_target_environment(macabi)
// CHECK: interface.m:[[@LINE+2]]:12: warning: 'A2' has incompatible definitions
// CHECK: interface.m:[[@LINE+1]]:12: note: interface 'A2' has no corresponding super class here
@interface A2 @end;
#else
// CHECK: interface.m:[[@LINE+1]]:17: note: interface 'A2' has super class 'Base2' here
@interface A2 : Base2 @end;
#endif

#if !__is_target_environment(macabi)
// CHECK: interface.m:[[@LINE+2]]:12: warning: 'A3' has incompatible definitions
// CHECK: interface.m:[[@LINE+1]]:16: note: 'A3' conforms to protocol 'P1'
@interface A3 <P1> @end;
#else
// CHECK: interface.m:[[@LINE+1]]:1: note: 'A3' has no corresponding protocol here
@interface A3 @end;
#endif

#if !__is_target_environment(macabi)
// CHECK: interface.m:[[@LINE+2]]:12: warning: 'A4' has incompatible definitions
// CHECK: interface.m:[[@LINE+1]]:1: note: 'A4' has no corresponding protocol here
@interface A4 @end;
#else
// CHECK: interface.m:[[@LINE+1]]:16: note: 'A4' conforms to protocol 'P2'
@interface A4 <P2> @end;
#endif

#if !__is_target_environment(macabi)
// CHECK: interface.m:[[@LINE+2]]:12: warning: 'A5' has incompatible definitions
// CHECK: interface.m:[[@LINE+1]]:16: note: 'A5' conforms to protocol 'P1'
@interface A5 <P1, P2> @end;
#else
// CHECK: interface.m:[[@LINE+1]]:1: note: 'A5' has no corresponding protocol here
@interface A5 <P2> @end;
#endif

// CHECK: interface.m:[[@LINE+2]]:12: warning: 'B' has incompatible definitions
// CHECK: interface.m:[[@LINE+1]]:16: note: interface 'B' has inconsistent super class here
@interface B : A @end;

// CHECK-NOT: 'C' has incompatible definitions
@interface C : Base1 @end

#if !__is_target_environment(macabi)
// CHECK-NOT: warning: 'M' has incompatible definitions
@interface M
-(void) a;
@end;
#else
@interface M
@end;
#endif

#if !__is_target_environment(macabi)
// CHECK-NOT: warning: 'M2' has incompatible definitions
@interface M2 @end;
#else
@interface M2
+(void) a;
@end;
#endif

#if !__is_target_environment(macabi)
// CHECK-NOT: warning: 'P' has incompatible definitions
@interface P
@property int a;
@end
#else
@interface P
@property int b;
@end
#endif

#if !__is_target_environment(macabi)
// CHECK: interface.m:[[@LINE+1]]:12: warning: 'I' has incompatible definitions
@interface I {
// CHECK: interface.m:[[@LINE+1]]:7: note: 'I' has ivar 'a' here
  int a;
}
@end
#else
// CHECK: interface.m:[[@LINE+1]]:1: note: 'I' has no corresponding ivar here
@interface I @end
#endif

#if !__is_target_environment(macabi)
// CHECK: interface.m:[[@LINE+1]]:12: warning: 'I1' has incompatible definitions
@interface I1 {
// CHECK: interface.m:[[@LINE+1]]:7: note: 'I1' has ivar 'a' here
  int a;
}
@end
#else
@interface I1 {
// CHECK: interface.m:[[@LINE+1]]:7: note: 'I1' has ivar 'b' here
  int b;
}
@end
#endif

#if !__is_target_environment(macabi)
// CHECK: interface.m:[[@LINE+2]]:12: warning: 'I2' has incompatible definitions
// CHECK: interface.m:[[@LINE+1]]:1: note: 'I2' has no corresponding ivar here
@interface I2 {
  int a;
}
@end
#else
@interface I2 {
  int a;
// CHECK: interface.m:[[@LINE+1]]:7: note: 'I2' has ivar 'b' here
  int b;
}
@end
#endif
