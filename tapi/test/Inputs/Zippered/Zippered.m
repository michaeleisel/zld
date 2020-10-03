#import "Zippered.h"
#import "Zippered_Private.h"

MyType invalidAPI() { return 0; }
int macOSAPI() { return 0; }
int iOSAPI() { return 0; }
int commonAPI() { return 0; }
int obsoletedMacOSAPI() { return 0; }

#if __is_target_environment(macabi)
int a = 0;
UIImage *image = 0;
#else
long a = 0;
NSImage *image = 0;
#endif
