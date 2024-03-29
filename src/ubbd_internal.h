#ifndef UBBD_INTERNAL_H
#define UBBD_INTERNAL_H

#include <linux/bsearch.h>
#include <linux/xarray.h>

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/blk-mq.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <net/genetlink.h>

#include <linux/types.h>

#include "ubbd.h"
#include "compat.h"

#define DEV_NAME_LEN 32
#define UBBD_SINGLE_MAJOR_PART_SHIFT 4
#define UBBD_DRV_NAME "ubbd"

#define UBBD_KRING_DATA_PAGES	(256 * 1024)
#define UBBD_KRING_DATA_RESERVE_PERCENT	75

/* request stats */
#ifdef UBBD_REQUEST_STATS
#define ubbd_req_stats_ktime_get(V) V = ktime_get() 
#define ubbd_req_stats_ktime_aggregate(T, D) T = ktime_add(T, D)
#define ubbd_req_stats_ktime_delta(V, ST) V = ktime_sub(ktime_get(), ST)
#else
#define ubbd_req_stats_ktime_get(V)
#define ubbd_req_stats_ktime_aggregate(T, D)
#define ubbd_req_stats_ktime_delta(V, ST)
#endif /* UBBD_REQUEST_STATS */

extern struct workqueue_struct *ubbd_wq;


#include <linux/device.h>
#include <linux/fs.h>
#include <linux/interrupt.h>

struct module;
struct ubbd_kring_map;

/**
 * struct ubbd_kring_mem - description of a KRING memory region
 * @name:		name of the memory region for identification
 * @addr:               address of the device's memory rounded to page
 *			size (phys_addr is used since addr can be
 *			logical, virtual, or physical & phys_addr_t
 *			should always be large enough to handle any of
 *			the address types)
 * @offs:               offset of device memory within the page
 * @size:		size of IO (multiple of page size)
 * @map:		for use by the KRING core only.
 */
struct ubbd_kring_mem {
	const char		*name;
	phys_addr_t		addr;
	unsigned long		offs;
	resource_size_t		size;
	struct ubbd_kring_map		*map;
};

#define MAX_KRING_MAPS	5

struct ubbd_kring_device {
	struct module           *owner;
	struct device		dev;
	int                     minor;
	atomic_t                event;
	struct fasync_struct    *async_queue;
	wait_queue_head_t       wait;
	struct ubbd_kring_info         *info;
	struct mutex		info_lock;
	struct kobject          *map_dir;
};

/**
 * struct ubbd_kring_info - KRING device capabilities
 * @ubbd_kring_dev:		the KRING device this info belongs to
 * @name:		device name
 * @version:		device driver version
 * @mem:		list of mappable memory regions, size==0 for end of list
 * @priv:		optional private data
 * @handler:		the device's irq handler
 * @mmap:		mmap operation for this ubbd_kring device
 * @open:		open operation for this ubbd_kring device
 * @release:		release operation for this ubbd_kring device
 * @irqcontrol:		disable/enable irqs when 0/1 is written to /dev/ubbd_kringX
 */
struct ubbd_kring_info {
	struct ubbd_kring_device	*ubbd_kring_dev;
	const char		*name;
	const char		*version;
	struct ubbd_kring_mem		mem[MAX_KRING_MAPS];
	void			*priv;
	int (*mmap)(struct ubbd_kring_info *info, struct vm_area_struct *vma);
	int (*open)(struct ubbd_kring_info *info, struct inode *inode);
	int (*release)(struct ubbd_kring_info *info, struct inode *inode);
	int (*irqcontrol)(struct ubbd_kring_info *info, s32 irq_on);
};

extern int __must_check
	__ubbd_kring_register_device(struct module *owner,
			      struct device *parent,
			      struct ubbd_kring_info *info);

/* use a define to avoid include chaining to get THIS_MODULE */

/**
 * ubbd_kring_register_device - register a new userspace IO device
 * @parent:	parent device
 * @info:	KRING device capabilities
 *
 * returns zero on success or a negative error code.
 */
#define ubbd_kring_register_device(parent, info) \
	__ubbd_kring_register_device(THIS_MODULE, parent, info)

extern void ubbd_kring_unregister_device(struct ubbd_kring_info *info);
extern void ubbd_kring_event_notify(struct ubbd_kring_info *info);

extern int __must_check
	__devm_ubbd_kring_register_device(struct module *owner,
				   struct device *parent,
				   struct ubbd_kring_info *info);

/* use a define to avoid include chaining to get THIS_MODULE */

/**
 * devm_ubbd_kring_register_device - Resource managed ubbd_kring_register_device()
 * @parent:	parent device
 * @info:	KRING device capabilities
 *
 * returns zero on success or a negative error code.
 */
#define devm_ubbd_kring_register_device(parent, info) \
	__devm_ubbd_kring_register_device(THIS_MODULE, parent, info)

struct ubbd_queue {
	struct ubbd_device	*ubbd_dev;

	int			index;
	struct list_head	inflight_reqs;
	spinlock_t		inflight_reqs_lock;
	u64			req_tid;

	struct ubbd_kring_info		ubbd_kring_info;
	struct xarray		data_pages_array;
	unsigned long		*data_bitmap;
	struct mutex		pages_mutex;

	struct ubbd_sb		*sb_addr;

	void			*cmdr;
	void			*compr;
	spinlock_t		cmdr_lock;
	spinlock_t		compr_lock;
	size_t			data_off;
	u32			data_pages;
	u32			data_pages_allocated;
	u32			data_pages_reserved;
	uint32_t		max_blocks;
	size_t			mmap_pages;

	struct mutex 		state_lock;
	unsigned long		flags;
	atomic_t		status;

	struct inode		*inode;
	struct work_struct	complete_work;
	cpumask_t		cpumask;
	pid_t			backend_pid;
	struct blk_mq_hw_ctx	*mq_hctx;

	struct dentry		*q_debugfs_d;
	struct dentry		*q_debugfs_status_f;
#ifdef	UBBD_REQUEST_STATS
	struct dentry		*q_debugfs_req_stats_f;

	uint64_t		stats_reqs;

	ktime_t			start_to_prepare;
	ktime_t			start_to_submit;

	ktime_t			start_to_complete;
	ktime_t			start_to_release;
#endif /* UBBD_REQUEST_STATS */
};

#define UBBD_QUEUE_FLAGS_HAS_BACKEND	1

struct ubbd_device {
	int			dev_id;		/* blkdev unique id */

	int			major;		/* blkdev assigned major */
	int			minor;
	struct gendisk		*disk;		/* blkdev's gendisk and rq */
	struct work_struct	work;		/* lifecycle work such add disk */

	char			name[DEV_NAME_LEN]; /* blkdev name, e.g. ubbd3 */

	spinlock_t		lock;		/* open_count */
	struct list_head	dev_node;	/* ubbd_dev_list */
	struct mutex		state_lock;

	/* Block layer tags. */
	struct blk_mq_tag_set	tag_set;

	struct dentry		*dev_debugfs_d;
	struct dentry		*dev_debugfs_queues_d;

	unsigned long		open_count;	/* protected by lock */

	uint32_t		num_queues;
	struct ubbd_queue	*queues;
	struct workqueue_struct	*task_wq;  /* workqueue for request work */

	u64			dev_size;
	u64			dev_features;
	u32			io_timeout;

	u8			status;
	u32			status_flags;
	struct kref		kref;
};

#define UBBD_DEV_STATUS_FLAG_INTRANS	1 << 0	/* bit in status_flags for is in state transition */

static inline bool ubbd_dev_status_flags_test(struct ubbd_device *ubbd_dev, u32 bit)
{
	return (ubbd_dev->status_flags >> UBBD_DEV_KSTATUS_SHIFT) & bit;
}

static inline void ubbd_dev_status_flags_set(struct ubbd_device *ubbd_dev, u32 bit)
{
	ubbd_dev->status_flags |= (bit << UBBD_DEV_KSTATUS_SHIFT);
}

static inline void ubbd_dev_status_flags_clear(struct ubbd_device *ubbd_dev, u32 bit)
{
	ubbd_dev->status_flags &= ~(bit << UBBD_DEV_KSTATUS_SHIFT);
}

struct ubbd_dev_add_opts {
	u32	data_pages;
	u64	device_size;
	u64	dev_features;
	u32	num_queues;
	u32	io_timeout;
};

struct ubbd_dev_config_opts {
	int	flags;
	u32	dp_reserve_percnt;
	u64	dev_size;
};

#define UBBD_DEV_CONFIG_FLAG_DP_RESERVE		1 << 0
#define UBBD_DEV_CONFIG_FLAG_DEV_SIZE		1 << 1

extern struct list_head ubbd_dev_list;
extern int ubbd_total_devs;
extern struct mutex ubbd_dev_list_mutex;

static inline int ubbd_dev_id_to_minor(int dev_id)
{
	return dev_id << UBBD_SINGLE_MAJOR_PART_SHIFT;
}

static inline int minor_to_ubbd_dev_id(int minor)
{
	return minor >> UBBD_SINGLE_MAJOR_PART_SHIFT;
}

void ubbd_dev_get(struct ubbd_device *ubbd_dev);
int ubbd_dev_get_unless_zero(struct ubbd_device *ubbd_dev);
void ubbd_dev_put(struct ubbd_device *ubbd_dev);
extern struct device *ubbd_kring_root_device;

#ifdef HAVE_BLK_FUNC_WITH_BD
static int ubbd_open(struct block_device *bdev, fmode_t mode)
{
	struct ubbd_device *ubbd_dev = bdev->bd_disk->private_data;
#else
static int ubbd_open(struct gendisk *disk, blk_mode_t mode)
{
	struct ubbd_device *ubbd_dev = disk->private_data;
#endif
	ubbd_dev_get(ubbd_dev);
	spin_lock_irq(&ubbd_dev->lock);
	ubbd_dev->open_count++;
	spin_unlock_irq(&ubbd_dev->lock);

	return 0;
}

#ifdef HAVE_BLK_RELEASE_WITHOUT_FLAGS
static void ubbd_release(struct gendisk *disk)
#else
static void ubbd_release(struct gendisk *disk, fmode_t mode)
#endif /* HAVE_BLK_RELEASE_WITHOUT_FLAGS */
{
	struct ubbd_device *ubbd_dev = disk->private_data;

	spin_lock_irq(&ubbd_dev->lock);
	ubbd_dev->open_count--;
	spin_unlock_irq(&ubbd_dev->lock);
	ubbd_dev_put(ubbd_dev);
}

static const struct block_device_operations ubbd_bd_ops = {
	.owner			= THIS_MODULE,
	.open			= ubbd_open,
	.release		= ubbd_release,
};


#define UBBD_REQ_INLINE_PI_MAX	4

struct ubbd_request {
	struct ubbd_queue	*ubbd_q;

	struct ubbd_se		*se;
	struct ubbd_ce		*ce;
	struct request		*req;

	enum ubbd_op		op;
	u64			req_tid;
	struct list_head	inflight_reqs_node;
	uint32_t		pi_cnt;
	uint32_t		inline_pi[UBBD_REQ_INLINE_PI_MAX];
	uint32_t		*pi;
	struct work_struct	work;

#ifdef	UBBD_REQUEST_STATS
	ktime_t			start_kt;

	ktime_t			start_to_prepare;
	ktime_t			start_to_submit;

	ktime_t			start_to_complete;
	ktime_t			start_to_release;
#endif
};

#define UPDATE_CMDR_HEAD(head, used, size) smp_store_release(&head, ((head % size) + used) % size)
#define UPDATE_CMDR_TAIL(tail, used, size) smp_store_release(&tail, ((tail % size) + used) % size)

#define UPDATE_COMPR_TAIL(tail, used, size) smp_store_release(&tail, ((tail % size) + used) % size)

static inline void ubbd_flush_dcache_range(void *vaddr, size_t size)
{
        unsigned long offset = offset_in_page(vaddr);
        void *start = vaddr - offset;

        size = round_up(size+offset, PAGE_SIZE);

        while (size) {
                flush_dcache_page(vmalloc_to_page(start));
                start += PAGE_SIZE;
                size -= PAGE_SIZE;
        }
}
extern struct genl_family ubbd_genl_family;
void complete_work_fn(struct work_struct *work);
blk_status_t ubbd_queue_rq(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd);
void ubbd_end_inflight_reqs(struct ubbd_device *ubbd_dev, int ret);
void ubbd_queue_end_inflight_reqs(struct ubbd_queue *ubbd_q, int ret);

#ifdef HAVE_TIMEOUT_RESERVED
enum blk_eh_timer_return ubbd_timeout(struct request *req, bool reserved);
#else
enum blk_eh_timer_return ubbd_timeout(struct request *req);
#endif /* HAVE_TIMEOUT_RESERVED */
struct ubbd_device *ubbd_dev_add_dev(struct ubbd_dev_add_opts *);
int ubbd_dev_remove_dev(struct ubbd_device *ubbd_dev);
int ubbd_dev_config(struct ubbd_device *ubbd_dev, struct ubbd_dev_config_opts *opts);
void ubbd_dev_remove_disk(struct ubbd_device *ubbd_dev, bool force);
int ubbd_dev_stop_queue(struct ubbd_device *ubbd_dev, int queue_id);
int ubbd_dev_start_queue(struct ubbd_device *ubbd_dev, int queue_id);
int ubbd_dev_add_disk(struct ubbd_device *ubbd_dev);
int ubbd_queue_kring_init(struct ubbd_queue *ubbd_q);
void ubbd_queue_kring_destroy(struct ubbd_queue *ubbd_q);
void ubbd_kring_unmap_range(struct ubbd_queue *ubbd_q,
		loff_t const holebegin, loff_t const holelen, int even_cows);

void ubbd_queue_complete(struct ubbd_queue *ubbd_q);

/* debugfs */
void ubbd_debugfs_add_dev(struct ubbd_device *ubbd_dev);
void ubbd_debugfs_remove_dev(struct ubbd_device *ubbd_dev);
void ubbd_debugfs_cleanup(void);
void __init ubbd_debugfs_init(void);

#undef UBBD_FAULT_INJECT

#ifdef UBBD_FAULT_INJECT
#define UBBD_REQ_FAULT_MASK	0x0fff	/* 1/4096 */
#define UBBD_MGMT_FAULT_MASK	0x00ff	/* 1/256 */

#include <linux/random.h>

static inline bool ubbd_req_need_fault(void)
{
	return ((get_random_u32() & UBBD_REQ_FAULT_MASK) == 1);
}

static inline bool ubbd_mgmt_need_fault(void)
{
	return ((get_random_u32() & UBBD_MGMT_FAULT_MASK) == 1);
}
#else
static inline bool ubbd_req_need_fault(void)
{
	return false;
}

static inline bool ubbd_mgmt_need_fault(void)
{
	return false;
}
#endif /* UBBD_FAULT_INJECT */

/* debug messages */
#define ubbd_err(fmt, ...)						\
	pr_err("ubbd: " fmt, ##__VA_ARGS__)
#define ubbd_info(fmt, ...)						\
	pr_info("ubbd: " fmt, ##__VA_ARGS__)
#define ubbd_debug(fmt, ...)						\
	pr_debug("ubbd: " fmt, ##__VA_ARGS__)

#define ubbd_dev_err(dev, fmt, ...)					\
	ubbd_err("ubbd%d: " fmt,					\
		 dev->dev_id, ##__VA_ARGS__)

#define ubbd_dev_info(dev, fmt, ...)					\
	ubbd_info("ubbd%d: " fmt,					\
		 dev->dev_id, ##__VA_ARGS__)

#define ubbd_dev_debug(dev, fmt, ...)					\
	ubbd_debug("ubbd%d: " fmt,					\
		 dev->dev_id, ##__VA_ARGS__)

#define ubbd_queue_err(queue, fmt, ...)					\
	ubbd_dev_err(queue->ubbd_dev, "queue-%d: " fmt,			\
		     queue->index, ##__VA_ARGS__)

#define ubbd_queue_info(queue, fmt, ...)					\
	ubbd_dev_info(queue->ubbd_dev, "queue-%d: " fmt,			\
		     queue->index, ##__VA_ARGS__)

#define ubbd_queue_debug(queue, fmt, ...)					\
	ubbd_dev_debug(queue->ubbd_dev, "queue-%d: " fmt,			\
		     queue->index, ##__VA_ARGS__)

extern int ubbd_major;
extern struct workqueue_struct *ubbd_wq;
extern struct ida ubbd_dev_id_ida;
extern struct device *ubbd_kring_root_device;
int ubbd_kring_init(void);
void ubbd_kring_exit(void);

static inline int __ubbd_init(void)
{
	int rc;

	ubbd_wq = alloc_workqueue(UBBD_DRV_NAME, WQ_MEM_RECLAIM, 0);
	if (!ubbd_wq) {
		rc = -ENOMEM;
		goto err;
	}

	ubbd_major = register_blkdev(0, UBBD_DRV_NAME);
	if (ubbd_major < 0) {
		rc = ubbd_major;
		goto err_out_wq;
	}

	rc = genl_register_family(&ubbd_genl_family);
	if (rc < 0) {
		goto err_out_blkdev;
	}

	rc = ubbd_kring_init();
	if (rc < 0) {
		goto err_out_genl;
	}

	ubbd_kring_root_device = root_device_register("ubbd_kring");
	if (IS_ERR(ubbd_kring_root_device)) {
		rc = PTR_ERR(ubbd_kring_root_device);
		goto err_exit_kring;
	}

	ubbd_debugfs_init();

	return 0;

err_exit_kring:
	ubbd_kring_exit();
err_out_genl:
	genl_unregister_family(&ubbd_genl_family);
err_out_blkdev:
	unregister_blkdev(ubbd_major, UBBD_DRV_NAME);
err_out_wq:
	destroy_workqueue(ubbd_wq);
err:
	return rc;
}

static inline void __ubbd_exit(void)
{
	ubbd_debugfs_cleanup();
	ida_destroy(&ubbd_dev_id_ida);
	genl_unregister_family(&ubbd_genl_family);
	root_device_unregister(ubbd_kring_root_device);
	unregister_blkdev(ubbd_major, UBBD_DRV_NAME);
	destroy_workqueue(ubbd_wq);
	ubbd_kring_exit();
}

#endif /* UBBD_INTERNAL_H */
