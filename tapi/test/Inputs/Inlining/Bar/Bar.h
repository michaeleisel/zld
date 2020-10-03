#ifndef INLINING_BAR_H
#define INLINING_BAR_H

#ifdef __i386__
extern int bar_sym1;
#else
extern int bar_sym2;
#endif

#endif // INLINING_BAR_H
