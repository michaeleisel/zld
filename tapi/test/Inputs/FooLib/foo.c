// xcrun clang -dynamiclib -install_name /usr/lib/libfoo.dylib -current_version
// 1 -compatibility_version 1 -o libfoo.dylib foo.c

int foo_public_func(int x) { return x + 1; }
int foo_private_func(int x) { return x + 2; }
