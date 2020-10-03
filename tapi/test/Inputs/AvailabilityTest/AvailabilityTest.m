#import <Foundation/Foundation.h>

// Public global variable
int publicGlobalVariable;

// Private global variable
int privateGlobalVariable;

// ObjC Class
@interface Foo : NSObject
@end

@implementation Foo
@end

#ifndef __i386__
int publicGlobalVariable2;
#endif

int publicGlobalVariable3;
int publicGlobalVariable4;

#ifndef __i386__
int publicGlobalVariable6;
#endif
