
#include <stdexcept>
#include <string>

extern bool gUsedFooNew;

bool bar()
{
	// std::string operations like append are implemented in libc++, so we can use them
	// to get a use of libc++.
	gUsedFooNew = false;
	std::string str;
	str.resize(10000);

	return gUsedFooNew;
}

