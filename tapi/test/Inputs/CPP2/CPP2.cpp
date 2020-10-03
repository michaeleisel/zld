#include "Templates.h"

// Templates
namespace templates {

template int foo1<short>(short a);
template <> int foo1<>(int a) { return a; }
template <class T> Foo<T>::Foo() {}
template <class T> Foo<T>::~Foo() {}

template <class T> Bar<T>::Bar() {}

template class Bar<int>;

} // end namespace templates.
