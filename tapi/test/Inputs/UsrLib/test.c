#include "umbrella.h"
#include "test.h"
#include "test_private.h"
#include "test_exclude.h"
#include "test_exclude2.h"

int public_function() { return 0; }
int private_function() { return 1; }
int exclude_function() { return 1; }
int exclude_function2() { return 2; }
