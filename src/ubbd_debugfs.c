#define pr_fmt(fmt)	KBUILD_MODNAME " debugfs: " fmt
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/jiffies.h>
#include <linux/list.h>

#include "ubbd_internal.h"

static struct dentry *ubbd_debugfs_root;
static struct dentry *ubbd_debugfs_devices;

static void ubbd_debugfs_remove(struct dentry **dp)
{
	debugfs_remove(*dp);
	*dp = NULL;
}

static int dev_attr_release(struct inode *inode, struct file *file)
{
	struct ubbd_queue *ubbd_q = inode->i_private;
	struct ubbd_device *ubbd_dev = ubbd_q->ubbd_dev;

	ubbd_dev_put(ubbd_dev);
	return single_release(inode, file);
}

#define UBBD_DEBUGFS_OPEN(NAME)								\
static int ubbd_ ## NAME ## _open(struct inode *inode, struct file *file) 		\
{											\
	struct ubbd_queue *ubbd_q = inode->i_private;					\
	struct ubbd_device *ubbd_dev = ubbd_q->ubbd_dev;				\
	struct dentry *parent;								\
	int ret = -ESTALE;								\
											\
	/* Are we still linked,								\
	 * or has debugfs_remove() already been called? */				\
	parent = file->f_path.dentry->d_parent;						\
	/* not sure if this can happen: */						\
	if (!parent || !parent->d_inode)						\
		goto out;								\
	/* serialize with d_delete() */							\
	inode_lock(d_inode(parent));							\
	/* Make sure the object is still alive */					\
	if (simple_positive(file->f_path.dentry)					\
	&& ubbd_dev_get_unless_zero(ubbd_dev))						\
		ret = 0;								\
	inode_unlock(d_inode(parent));							\
	if (!ret) {									\
		ret = single_open(file, ubbd_ ## NAME ## _show, ubbd_q);		\
		if (ret)								\
			ubbd_dev_put(ubbd_dev);						\
	}										\
out:											\
	return ret;									\
};

#define UBBD_DEBUGFS_RO_FILE(NAME)					\
UBBD_DEBUGFS_OPEN(NAME)							\
static const struct file_operations ubbd_ ## NAME ## _fops = {		\
	.owner		= THIS_MODULE,					\
	.open		= ubbd_ ## NAME ## _open,			\
	.read		= seq_read,					\
	.llseek		= seq_lseek,					\
	.release	= dev_attr_release,				\
};

#define UBBD_DEBUGFS_FILE(NAME)						\
UBBD_DEBUGFS_OPEN(NAME)							\
static const struct file_operations ubbd_ ## NAME ## _fops = {		\
	.owner		= THIS_MODULE,					\
	.open		= ubbd_ ## NAME ## _open,			\
	.write		= ubbd_ ## NAME ## _write,			\
	.read		= seq_read,					\
	.llseek		= seq_lseek,					\
	.release	= dev_attr_release,				\
};

static int ubbd_q_status_show(struct seq_file *file, void *ignored)
{
	struct ubbd_queue *ubbd_q = file->private;

	seq_printf(file,
		   "data_pages:			%12u\n"
		   "data_pages_reserved:	%12d\n"
		   "data_pages_allocated:	%12d\n",
		   ubbd_q->data_pages, ubbd_q->data_pages_reserved, ubbd_q->data_pages_allocated);
	seq_puts(file, "\n");

	return 0;
}

UBBD_DEBUGFS_RO_FILE(q_status);

#ifdef UBBD_REQUEST_STATS
static int ubbd_q_req_stats_show(struct seq_file *file, void *ignored)
{
	struct ubbd_queue *ubbd_q = file->private;
	uint64_t stats_reqs = ubbd_q->stats_reqs;
	uint64_t start_to_prepare = ubbd_q->start_to_prepare;
	uint64_t start_to_submit = ubbd_q->start_to_submit;
	uint64_t start_to_complete = ubbd_q->start_to_complete;
	uint64_t start_to_release = ubbd_q->start_to_release;

	if (stats_reqs) {
		do_div(start_to_prepare, stats_reqs);
		do_div(start_to_submit, stats_reqs);
		do_div(start_to_complete, stats_reqs);
		do_div(start_to_release, stats_reqs);
	}

	seq_printf(file,
		   "request stats values are nanoseconds; write an 'r' to reset all to 0\n\n"
		   "requests:		%12llu\n"
		   "start_to_prepare:	%12lld\n"
		   "start_to_submit:	%12lld\n"
		   "start_to_complete:	%12lld\n"
		   "start_to_release:	%12lld\n",
		   stats_reqs, start_to_prepare, start_to_submit, start_to_complete, start_to_release);
	seq_puts(file, "\n");

	return 0;
}

static ssize_t ubbd_q_req_stats_write(struct file *file, const char __user *ubuf,
				size_t cnt, loff_t *ppos)
{
	struct ubbd_queue *ubbd_q = file_inode(file)->i_private;
	char buffer;

	if (copy_from_user(&buffer, ubuf, 1))
		return -EFAULT;

	if (buffer == 'r' || buffer == 'R') {
		ubbd_q->stats_reqs = 0;
		ubbd_q->start_to_prepare = ns_to_ktime(0);
		ubbd_q->start_to_submit = ns_to_ktime(0);
		ubbd_q->start_to_complete = ns_to_ktime(0);
		ubbd_q->start_to_release = ns_to_ktime(0);
	}

	return cnt;
}

UBBD_DEBUGFS_FILE(q_req_stats);

#endif /* UBBD_REQUEST_STATS */

void ubbd_debugfs_add_dev(struct ubbd_device *ubbd_dev)
{
	int i;
	struct ubbd_queue *ubbd_q;
	char queue_id_buf[8];

	ubbd_dev->dev_debugfs_d = debugfs_create_dir(ubbd_dev->name, ubbd_debugfs_devices);
	ubbd_dev->dev_debugfs_queues_d = debugfs_create_dir("queues", ubbd_dev->dev_debugfs_d);

	for (i = 0; i < ubbd_dev->num_queues; i++) {
		ubbd_q = &ubbd_dev->queues[i];
		snprintf(queue_id_buf, sizeof(queue_id_buf), "%u", i);
		ubbd_q->q_debugfs_d = debugfs_create_dir(queue_id_buf, ubbd_dev->dev_debugfs_queues_d);
		ubbd_q->q_debugfs_status_f = debugfs_create_file("status", 0600,
				ubbd_q->q_debugfs_d, ubbd_q, &ubbd_q_status_fops);
#ifdef UBBD_REQUEST_STATS
		ubbd_q->q_debugfs_req_stats_f = debugfs_create_file("req_stats", 0600,
				ubbd_q->q_debugfs_d, ubbd_q, &ubbd_q_req_stats_fops);
#endif /* UBBD_REQUEST_STATS */
	}
}

void ubbd_debugfs_remove_dev(struct ubbd_device *ubbd_dev)
{
	int i;
	struct ubbd_queue *ubbd_q;

	for (i = 0; i < ubbd_dev->num_queues; i++) {
		ubbd_q = &ubbd_dev->queues[i];
#ifdef UBBD_REQUEST_STATS
		ubbd_debugfs_remove(&ubbd_q->q_debugfs_req_stats_f);
#endif /* UBBD_REQUEST_STATS */
		ubbd_debugfs_remove(&ubbd_q->q_debugfs_status_f);
		ubbd_debugfs_remove(&ubbd_q->q_debugfs_d);
	}

	ubbd_debugfs_remove(&ubbd_dev->dev_debugfs_queues_d);
	ubbd_debugfs_remove(&ubbd_dev->dev_debugfs_d);
}

void ubbd_debugfs_cleanup(void)
{
	ubbd_debugfs_remove(&ubbd_debugfs_devices);
	ubbd_debugfs_remove(&ubbd_debugfs_root);
}

void __init ubbd_debugfs_init(void)
{
	struct dentry *dentry;

	dentry = debugfs_create_dir("ubbd", NULL);
	ubbd_debugfs_root = dentry;

	dentry = debugfs_create_dir("devices", ubbd_debugfs_root);
	ubbd_debugfs_devices = dentry;
}
