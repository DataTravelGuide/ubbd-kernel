#include <linux/blkdev.h>

int main(void)
{
	blk_cleanup_disk(NULL);

	return 0;
}
