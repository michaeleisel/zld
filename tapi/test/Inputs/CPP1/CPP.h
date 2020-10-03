#ifndef CPP_H
#define CPP_H

inline int foo(int x) { return x + 1; }

extern int bar(int x) { return x + 1; }

inline int baz(int x) {
  static const int a[] = {1, 2, 3};
  return a[x];
}

class Bar {
public:
  static const int x = 0;
  static int y;

  inline int func1(int x) { return x + 2; }
  inline int func2(int x);
  int func3(int x);
};

class __attribute__((visibility("hidden"))) BarI {
  static const int x = 0;
  static int y;

  inline int func1(int x) { return x + 2; }
  inline int func2(int x);
  int func3(int x);
};

int Bar::func2(int x) { return x + 3; }
inline int Bar::func3(int x) { return x + 4; }

int BarI::func2(int x) { return x + 3; }
inline int BarI::func3(int x) { return x + 4; }
#endif
