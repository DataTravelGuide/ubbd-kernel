#include <linux/blkdev.h>

static void ubbd_release(struct gendisk *disk)
{
	return;
}

int main(void)
{
	struct block_device_operations ubbd_bd_ops = {
		.release		= ubbd_release,
	};

	return 0;
}
