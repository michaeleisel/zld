
// adding a thread_local makes the dylib never-unload
__thread int x = 1;

extern int cc();

int bb()
{
	return cc();
}

