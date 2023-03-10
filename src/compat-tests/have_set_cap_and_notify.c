#include <linux/blkdev.h>

int main(void)
{
	set_capacity_and_notify(NULL, 0);

	return 0;
}
