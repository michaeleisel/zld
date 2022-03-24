
#include <mach-o/dyld.h>
#include <mach-o/dyld_priv.h>

static void notify(const struct mach_header* mh, const char* path, bool unloadable)
{
}

void registerNotifier() {
	_dyld_register_for_image_loads(&notify);
}