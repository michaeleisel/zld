
extern int missing;
extern int foo();

int* missingPtr = &missing;

int baz()
{
	return *missingPtr + foo();
}