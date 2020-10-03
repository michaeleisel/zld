#ifndef PUBLIC_H
#define PUBLIC_H

extern int public_sym1;

__attribute__((visibility("default")))
@interface A
- (void) method;
@end

#endif
