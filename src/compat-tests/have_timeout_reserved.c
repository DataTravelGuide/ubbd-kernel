#include <linux/blk-mq.h>

static enum blk_eh_timer_return test_timeout(struct request *req, bool reserved)
{
	return BLK_EH_DONE;
}

int main(void)
{
	struct blk_mq_ops ops;

	ops.timeout = test_timeout;

	return 0;
}
