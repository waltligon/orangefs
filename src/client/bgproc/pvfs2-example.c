#include <limits.h>
#include <unistd.h>

int main(void)
{
	while (1)
		sleep(UINT_MAX);
	return 0;
}
