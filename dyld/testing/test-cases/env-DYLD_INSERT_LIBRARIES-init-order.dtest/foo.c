extern void fooInitCalled();

__attribute__((constructor))
static void fooInit()
{
    fooInitCalled();
}

