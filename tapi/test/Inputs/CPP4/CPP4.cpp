#define EXTERN_TEMPLATE(...)
#include "Templates.h"

namespace test2 {

template <> int &Foo<int>::instance() {
  static int a = 0;
  return a;
}

} // end namespace test2.

void test3::Foo<float>::run() {}
void test3::Foo<float>::run1() {}

__attribute__((visibility("hidden")))
void foobar() {
  test3::Foo<float>::run(); // use static method.
}

template<> void test4::foo<test4::A>() {}
template<> void test4::bar<test4::A>() {}

template <> test5::IntPtr test5::e<>::foo(test5::MyType &) const {
  return test5::IntPtr();
}

template <> test5::IntPtr test5::e<>::bar(test5::MyType &t) const {
  return foo(t);
}

int test5::e<int>::bar(test5::MyType &t) const {
  return foo(t);
}

float test5::e<float>::bar(test5::MyType &t) const {
  return foo(t);
}
