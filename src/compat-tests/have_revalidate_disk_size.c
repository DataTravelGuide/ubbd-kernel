#include <linux/blkdev.h>

int main(void)
{
	revalidate_disk_size(NULL, true);

	return 0;
}
