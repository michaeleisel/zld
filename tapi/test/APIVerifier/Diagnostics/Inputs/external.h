#if !__is_target_environment(macabi)
typedef int MyType;
#else
typedef float MyType;
#endif

typedef double MyType2;

@protocol B
@end

@protocol C
@end

#if !__is_target_environment(macabi)
@interface A <B>
#else
@interface A <B, C>
#endif
- (int)method;
@end

#if !__is_target_environment(macabi)
typedef struct S1 {
  int a;
} MyStruct;
#else
typedef struct S2 {
  float b;
} MyStruct;
#endif
