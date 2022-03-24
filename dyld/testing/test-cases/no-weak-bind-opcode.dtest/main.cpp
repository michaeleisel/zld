// BUILD:  $CC main.cpp -o $BUILD_DIR/no-weak-bind-opcode.exe

// RUN:  ./no-weak-bind-opcode.exe

#include <stdio.h>
#include <stdlib.h>

#include "test_support.h"

struct A {
public:
  static int getStaticValue() { return 5; }
};

int main() {
    int ret = A::getStaticValue();

    if ( ret != 5 )
        FAIL("wrong static value");
    //Succeeds if no crash occured
    PASS("Success");
    
}
