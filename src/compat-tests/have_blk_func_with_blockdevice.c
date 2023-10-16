#include <linux/blkdev.h>

static int ubbd_open(struct block_device *bdev, fmode_t mode)
{
	return 0;
}

int main(void)
{
	struct block_device_operations ubbd_bd_ops = {
		.open			= ubbd_open,
	};

	return 0;
}
