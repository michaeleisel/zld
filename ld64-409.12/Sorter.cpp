#include "Sorter.hpp"
#include "pstl/execution"
#include "pstl/algorithm"
#include "pstl/memory"
#include "pstl/numeric"
#include <vector>
#include <algorithm>

void a() {
	std::vector<int> vec;
	std::sort(std::execution::unseq, vec.begin(), vec.end());
}
