
extern void bar() __attribute__((weak_import));

void foo()
{
	if (&bar)
		bar();
}


