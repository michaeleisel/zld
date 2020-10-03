#ifndef INLINING_FOO_H
#define INLINING_FOO_H

#ifdef __i386__
extern int foo_sym1;
#else
extern int foo_sym2;
#endif

#endif // INLINING_FOO_H
