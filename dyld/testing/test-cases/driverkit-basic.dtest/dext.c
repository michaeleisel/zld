#include <mach-o/dyld_priv.h>
#include <os/log.h>

int dext_main(void)
{
    os_log(OS_LOG_DEFAULT, "dyld-driverkit-basic");
    return 24;
}

__attribute__((constructor))
void init(void)
{
    _dyld_register_driverkit_main((void (*)())dext_main);
}
