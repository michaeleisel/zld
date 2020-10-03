#ifndef TEMPLATES_H
#define TEMPLATES_H

#ifndef EXTERN_TEMPLATE
#define EXTERN_TEMPLATE(...) extern template __VA_ARGS__;
#endif

namespace test1 {

template <class A = int> class Foo {
public:
  static A &run() { return instance(); }
  static short getX();

private:
  Foo() = delete;
  static A &instance();
  static short x;
};

template <class A> short Foo<A>::x = 0;

template <class A> inline short Foo<A>::getX() { return x; }

} // end namespace test1.

namespace test2 {

template <class A = int> class Foo {
public:
  static A &run() { return instance(); }
  static short getX();

private:
  Foo() = delete;
  static A &instance();
  static short x;
};

template <class A> short Foo<A>::x = 0;

template <class A> inline short Foo<A>::getX() { return x; }

EXTERN_TEMPLATE(int &Foo<int>::instance())

} // end namespace test2.

// Check template specialization.
namespace test3 {

template <typename A>
class Foo {
public:
  static void run();
};

template<>
class Foo<int> {
public:
  static void run() {}
};

template<>
class Foo<float> {
public:
  static void run();
  static void run1();
};

} // end namespace test3.

#define SYM_HIDDEN __attribute__((visibility("hidden")))
#define SYM_EXPORT __attribute__((visibility("default")))

namespace test4 {
class SYM_HIDDEN A {};
template<class T> void foo();
template<> void foo<A>();

template<class T> void bar();
template<> void bar<A>() SYM_EXPORT;

} // end namespace test4

namespace test5 {

template <class> class SYM_HIDDEN shared_ptr {};
using IntPtr = shared_ptr<int>;
class SYM_HIDDEN MyType;
template <class T = IntPtr> class SYM_EXPORT e {
  virtual T foo(MyType &) const;
  virtual T bar(MyType &) const;
};
template <> IntPtr e<>::foo(MyType &) const SYM_HIDDEN;
template <> IntPtr e<>::bar(MyType &) const SYM_EXPORT;

template <>
class SYM_HIDDEN e<int> {
  virtual int foo(MyType &) const SYM_EXPORT { return 0; } // this is not exported?
  virtual int bar(MyType &) const SYM_EXPORT;
};

template <>
class SYM_EXPORT e<float> {
  virtual float foo(MyType &) const SYM_EXPORT { return 0.0f; }
  virtual float bar(MyType &) const SYM_EXPORT;
};

} // namespace test5

namespace test6 {
template <typename T> class Foo {
public:
  void setT(T t);

private:
  T _v;
};

template <typename T> void Foo<T>::setT(T t) { _v = t; }

} // namespace test6

#endif
