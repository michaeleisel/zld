#ifndef TEMPLATES_H
#define TEMPLATES_H

namespace templates {

// Full specialization.
template <class T> int foo1(T a) { return 1; }
template <> int foo1<int>(int a);
extern template int foo1<short>(short a);

template <class T> int foo2(T a);

// Partial specialization.
template <class A, class B> class Partial {
  static int run(A a, B b) { return a + b; }
};

template <class A> class Partial<A, int> {
  static int run(A a, int b) { return a - b; }
};

template <class T> class Foo {
public:
  Foo();
  ~Foo();
};

template <class T> class Bar {
public:
  Bar();
  ~Bar() {}

  inline int bazinga() { return 7; }
};

extern template class Bar<int>;

class Bazz {
public:
  Bazz() {}

  template <class T> int buzz(T a);

  float implicit() const { return foo1(0.0f); }
};

template <class T> int Bazz::buzz(T a) { return sizeof(T); }

template <class T> struct S { static int x; };

template <class T> int S<T>::x = 0;

} // end namespace templates.

#endif
