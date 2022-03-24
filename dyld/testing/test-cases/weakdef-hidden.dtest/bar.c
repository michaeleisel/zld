
__attribute__((weak))
int answer() {
	return 42;
}

int bar_answer() {
	return answer();
}

