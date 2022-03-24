
extern int bar();

__attribute__((weak_import))
extern int nullable();

int foo() {
	return bar();
}

void* getNullable() {
	return (void*)&nullable;
}