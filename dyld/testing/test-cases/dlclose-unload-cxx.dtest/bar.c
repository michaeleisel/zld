
static void local()
{
}

extern void* common() __attribute__((weak));

void* common() 
{
	return &local;
}

void* bar()
{
	return common();
}
