#if !__is_target_environment(macabi)
typedef int MyType;
#else
typedef float MyType;
#endif
