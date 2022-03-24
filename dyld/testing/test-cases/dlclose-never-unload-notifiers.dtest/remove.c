
#include <mach-o/dyld.h>
#include <mach-o/dyld_priv.h>

static void notify(const struct mach_header* mh, intptr_t vmaddr_slide)
{
}

void registerNotifier() {
	_dyld_register_func_for_remove_image(&notify);
}