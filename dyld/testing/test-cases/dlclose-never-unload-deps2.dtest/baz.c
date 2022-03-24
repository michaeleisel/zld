
extern int foo();
typedef __typeof(&foo) fooPtr;

fooPtr baz()
{
	return &foo;
}

