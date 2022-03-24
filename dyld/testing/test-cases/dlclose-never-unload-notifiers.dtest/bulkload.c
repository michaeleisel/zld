
#include <mach-o/dyld.h>
#include <mach-o/dyld_priv.h>

static void notify(unsigned count, const struct mach_header* mhs[], const char* paths[])
{
}

void registerNotifier() {
	_dyld_register_for_bulk_image_loads(&notify);
}