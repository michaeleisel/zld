#include "VTable.h"

// VTables
void test1::Simple::run() {}
void test2::Simple::run() {}
void test4::Sub::run() {}
void test5::Sub::run() {}
void test6::Sub::run() {}
void test11::Sub::run3() {}
void test13::Base::run3() {}

test14::Base::~Base() {}
test14::Sub::Sub() {}
test14::Sub::~Sub() {}
void test14::Sub::run1() const {}

test15::Sub::~Sub() {}
void test15::Sub::run() {}
void test15::Sub::run1() {}
void test15::Sub1::run() {}
void test15::Sub1::run1() {}

template class test12::Simple<int>;

// Weak-Defined RTTI
__attribute__((visibility("hidden"))) test7::Sub a;
