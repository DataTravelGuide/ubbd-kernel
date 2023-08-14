/*
 * Provide a kring to share memory with userspace process.
 *
 * Copyright(C) 2023, Dongsheng Yang <dongsheng.yang.linux@gmail.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/idr.h>
#include <linux/sched/signal.h>
#include <linux/string.h>
#include <linux/kobject.h>
#include <linux/cdev.h>
#include "ubbd_internal.h"

#define KRING_MAX_DEVICES		(1U << MINORBITS)

static int ubbd_kring_major;
static struct cdev *ubbd_kring_cdev;
static DEFINE_IDR(ubbd_kring_idr);
static const struct file_operations ubbd_kring_fops;

/* Protect idr accesses */
static DEFINE_MUTEX(minor_lock);

/*
 * attributes
 */
static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct ubbd_kring_device *idev = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&idev->info_lock);
	if (!idev->info) {
		ret = -EINVAL;
		dev_err(dev, "the device has been unregistered\n");
		goto out;
	}

	ret = sprintf(buf, "%s\n", idev->info->name);

out:
	mutex_unlock(&idev->info_lock);
	return ret;
}
static DEVICE_ATTR_RO(name);

static ssize_t version_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct ubbd_kring_device *idev = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&idev->info_lock);
	if (!idev->info) {
		ret = -EINVAL;
		dev_err(dev, "the device has been unregistered\n");
		goto out;
	}

	ret = sprintf(buf, "%s\n", idev->info->version);

out:
	mutex_unlock(&idev->info_lock);
	return ret;
}
static DEVICE_ATTR_RO(version);

static ssize_t event_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct ubbd_kring_device *idev = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", (unsigned int)atomic_read(&idev->event));
}
static DEVICE_ATTR_RO(event);

static struct attribute *ubbd_kring_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_version.attr,
	&dev_attr_event.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ubbd_kring);

/* KRING class infrastructure */
static struct class ubbd_kring_class = {
	.name = "ubbd_kring",
	.dev_groups = ubbd_kring_groups,
};

static bool ubbd_kring_class_registered;

/*
 * device functions
 */
static int ubbd_kring_get_minor(struct ubbd_kring_device *idev)
{
	int retval;

	mutex_lock(&minor_lock);
	retval = idr_alloc(&ubbd_kring_idr, idev, 0, KRING_MAX_DEVICES, GFP_KERNEL);
	if (retval >= 0) {
		idev->minor = retval;
		retval = 0;
	} else if (retval == -ENOSPC) {
		dev_err(&idev->dev, "too many ubbd_kring devices\n");
		retval = -EINVAL;
	}
	mutex_unlock(&minor_lock);
	return retval;
}

static void ubbd_kring_free_minor(unsigned long minor)
{
	mutex_lock(&minor_lock);
	idr_remove(&ubbd_kring_idr, minor);
	mutex_unlock(&minor_lock);
}

/**
 * ubbd_kring_event_notify - trigger an interrupt event
 * @info: KRING device capabilities
 */
void ubbd_kring_event_notify(struct ubbd_kring_info *info)
{
	struct ubbd_kring_device *idev = info->ubbd_kring_dev;

	atomic_inc(&idev->event);
	wake_up_interruptible(&idev->wait);
}
EXPORT_SYMBOL_GPL(ubbd_kring_event_notify);

struct ubbd_kring_listener {
	struct ubbd_kring_device *dev;
	s32 event_count;
};

static int ubbd_kring_open(struct inode *inode, struct file *filep)
{
	struct ubbd_kring_device *idev;
	struct ubbd_kring_listener *listener;
	int ret = 0;

	mutex_lock(&minor_lock);
	idev = idr_find(&ubbd_kring_idr, iminor(inode));
	mutex_unlock(&minor_lock);
	if (!idev) {
		ret = -ENODEV;
		goto out;
	}

	get_device(&idev->dev);

	if (!try_module_get(idev->owner)) {
		ret = -ENODEV;
		goto err_module_get;
	}

	listener = kmalloc(sizeof(*listener), GFP_KERNEL);
	if (!listener) {
		ret = -ENOMEM;
		goto err_alloc_listener;
	}

	listener->dev = idev;
	listener->event_count = atomic_read(&idev->event);
	filep->private_data = listener;

	mutex_lock(&idev->info_lock);
	if (!idev->info) {
		mutex_unlock(&idev->info_lock);
		ret = -EINVAL;
		goto err_infoopen;
	}

	if (idev->info->open)
		ret = idev->info->open(idev->info, inode);
	mutex_unlock(&idev->info_lock);
	if (ret)
		goto err_infoopen;

	return 0;

err_infoopen:
	kfree(listener);

err_alloc_listener:
	module_put(idev->owner);

err_module_get:
	put_device(&idev->dev);

out:
	return ret;
}

static int ubbd_kring_fasync(int fd, struct file *filep, int on)
{
	struct ubbd_kring_listener *listener = filep->private_data;
	struct ubbd_kring_device *idev = listener->dev;

	return fasync_helper(fd, filep, on, &idev->async_queue);
}

static int ubbd_kring_release(struct inode *inode, struct file *filep)
{
	int ret = 0;
	struct ubbd_kring_listener *listener = filep->private_data;
	struct ubbd_kring_device *idev = listener->dev;

	mutex_lock(&idev->info_lock);
	if (idev->info && idev->info->release)
		ret = idev->info->release(idev->info, inode);
	mutex_unlock(&idev->info_lock);

	module_put(idev->owner);
	kfree(listener);
	put_device(&idev->dev);
	return ret;
}

static __poll_t ubbd_kring_poll(struct file *filep, poll_table *wait)
{
	struct ubbd_kring_listener *listener = filep->private_data;
	struct ubbd_kring_device *idev = listener->dev;

	poll_wait(filep, &idev->wait, wait);
	if (listener->event_count != atomic_read(&idev->event)) {
		listener->event_count = atomic_read(&idev->event);
		return EPOLLIN | EPOLLRDNORM;
	}
	return 0;
}

static ssize_t ubbd_kring_read(struct file *filep, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct ubbd_kring_listener *listener = filep->private_data;
	struct ubbd_kring_device *idev = listener->dev;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t retval = 0;
	s32 event_count;

	if (count != sizeof(s32))
		return -EINVAL;

	add_wait_queue(&idev->wait, &wait);

	do {
		mutex_lock(&idev->info_lock);
		if (!idev->info) {
			retval = -EIO;
			mutex_unlock(&idev->info_lock);
			break;
		}
		mutex_unlock(&idev->info_lock);

		set_current_state(TASK_INTERRUPTIBLE);

		event_count = atomic_read(&idev->event);
		if (event_count != listener->event_count) {
			__set_current_state(TASK_RUNNING);
			if (copy_to_user(buf, &event_count, count))
				retval = -EFAULT;
			else {
				listener->event_count = event_count;
				retval = count;
			}
			break;
		}

		if (filep->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	} while (1);

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&idev->wait, &wait);

	return retval;
}

static ssize_t ubbd_kring_write(struct file *filep, const char __user *buf,
			size_t count, loff_t *ppos)
{
	struct ubbd_kring_listener *listener = filep->private_data;
	struct ubbd_kring_device *idev = listener->dev;
	ssize_t retval;

	retval = idev->info->irqcontrol(idev->info, 0);

	return retval ? retval : sizeof(s32);
}

static int ubbd_kring_mmap(struct file *filep, struct vm_area_struct *vma)
{
	struct ubbd_kring_listener *listener = filep->private_data;
	struct ubbd_kring_device *idev = listener->dev;
	unsigned long requested_pages, actual_pages;
	int ret = 0;

	if (vma->vm_end < vma->vm_start)
		return -EINVAL;

	vma->vm_private_data = idev;

	mutex_lock(&idev->info_lock);
	if (!idev->info) {
		ret = -EINVAL;
		goto out;
	}

	requested_pages = vma_pages(vma);
	actual_pages = ((idev->info->mem[0].addr & ~PAGE_MASK)
			+ idev->info->mem[0].size + PAGE_SIZE -1) >> PAGE_SHIFT;
	if (requested_pages > actual_pages) {
		ret = -EINVAL;
		goto out;
	}

	if (idev->info->mmap) {
		ret = idev->info->mmap(idev->info, vma);
		goto out;
	}

	ret = -EINVAL;

 out:
	mutex_unlock(&idev->info_lock);
	return ret;
}

static const struct file_operations ubbd_kring_fops = {
	.owner		= THIS_MODULE,
	.open		= ubbd_kring_open,
	.release	= ubbd_kring_release,
	.read		= ubbd_kring_read,
	.write		= ubbd_kring_write,
	.mmap		= ubbd_kring_mmap,
	.poll		= ubbd_kring_poll,
	.fasync		= ubbd_kring_fasync,
	.llseek		= noop_llseek,
};

static int ubbd_kring_major_init(void)
{
	static const char name[] = "ubbd_kring";
	struct cdev *cdev = NULL;
	dev_t ubbd_kring_dev = 0;
	int result;

	result = alloc_chrdev_region(&ubbd_kring_dev, 0, KRING_MAX_DEVICES, name);
	if (result)
		goto out;

	result = -ENOMEM;
	cdev = cdev_alloc();
	if (!cdev)
		goto out_unregister;

	cdev->owner = THIS_MODULE;
	cdev->ops = &ubbd_kring_fops;
	kobject_set_name(&cdev->kobj, "%s", name);

	result = cdev_add(cdev, ubbd_kring_dev, KRING_MAX_DEVICES);
	if (result)
		goto out_put;

	ubbd_kring_major = MAJOR(ubbd_kring_dev);
	ubbd_kring_cdev = cdev;
	return 0;
out_put:
	kobject_put(&cdev->kobj);
out_unregister:
	unregister_chrdev_region(ubbd_kring_dev, KRING_MAX_DEVICES);
out:
	return result;
}

static void ubbd_kring_major_cleanup(void)
{
	unregister_chrdev_region(MKDEV(ubbd_kring_major, 0), KRING_MAX_DEVICES);
	cdev_del(ubbd_kring_cdev);
}

static int init_ubbd_kring_class(void)
{
	int ret;

	/* This is the first time in here, set everything up properly */
	ret = ubbd_kring_major_init();
	if (ret)
		goto exit;

	ret = class_register(&ubbd_kring_class);
	if (ret) {
		printk(KERN_ERR "class_register failed for ubbd_kring\n");
		goto err_class_register;
	}

	ubbd_kring_class_registered = true;

	return 0;

err_class_register:
	ubbd_kring_major_cleanup();
exit:
	return ret;
}

static void release_ubbd_kring_class(void)
{
	ubbd_kring_class_registered = false;
	class_unregister(&ubbd_kring_class);
	ubbd_kring_major_cleanup();
}

static void ubbd_kring_device_release(struct device *dev)
{
	struct ubbd_kring_device *idev = dev_get_drvdata(dev);

	kfree(idev);
}

/**
 * __ubbd_kring_register_device - register a new userspace IO device
 * @owner:	module that creates the new device
 * @parent:	parent device
 * @info:	KRING device capabilities
 *
 * returns zero on success or a negative error code.
 */
int __ubbd_kring_register_device(struct module *owner,
			  struct device *parent,
			  struct ubbd_kring_info *info)
{
	struct ubbd_kring_device *idev;
	int ret = 0;

	if (!ubbd_kring_class_registered)
		return -EPROBE_DEFER;

	if (!parent || !info || !info->name || !info->version)
		return -EINVAL;

	info->ubbd_kring_dev = NULL;

	idev = kzalloc(sizeof(*idev), GFP_KERNEL);
	if (!idev) {
		return -ENOMEM;
	}

	idev->owner = owner;
	idev->info = info;
	mutex_init(&idev->info_lock);
	init_waitqueue_head(&idev->wait);
	atomic_set(&idev->event, 0);

	ret = ubbd_kring_get_minor(idev);
	if (ret) {
		kfree(idev);
		return ret;
	}

	device_initialize(&idev->dev);
	idev->dev.devt = MKDEV(ubbd_kring_major, idev->minor);
	idev->dev.class = &ubbd_kring_class;
	idev->dev.parent = parent;
	idev->dev.release = ubbd_kring_device_release;
	dev_set_drvdata(&idev->dev, idev);

	ret = dev_set_name(&idev->dev, "ubbd_kring%d", idev->minor);
	if (ret)
		goto err_device_create;

	ret = device_add(&idev->dev);
	if (ret)
		goto err_device_create;

	info->ubbd_kring_dev = idev;
	return 0;

err_device_create:
	ubbd_kring_free_minor(idev->minor);
	put_device(&idev->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(__ubbd_kring_register_device);

static void devm_ubbd_kring_unregister_device(struct device *dev, void *res)
{
	ubbd_kring_unregister_device(*(struct ubbd_kring_info **)res);
}

/**
 * __devm_ubbd_kring_register_device - Resource managed ubbd_kring_register_device()
 * @owner:	module that creates the new device
 * @parent:	parent device
 * @info:	KRING device capabilities
 *
 * returns zero on success or a negative error code.
 */
int __devm_ubbd_kring_register_device(struct module *owner,
			       struct device *parent,
			       struct ubbd_kring_info *info)
{
	struct ubbd_kring_info **ptr;
	int ret;

	ptr = devres_alloc(devm_ubbd_kring_unregister_device, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	*ptr = info;
	ret = __ubbd_kring_register_device(owner, parent, info);
	if (ret) {
		devres_free(ptr);
		return ret;
	}

	devres_add(parent, ptr);

	return 0;
}
EXPORT_SYMBOL_GPL(__devm_ubbd_kring_register_device);

/**
 * ubbd_kring_unregister_device - unregister a industrial IO device
 * @info:	KRING device capabilities
 *
 */
void ubbd_kring_unregister_device(struct ubbd_kring_info *info)
{
	struct ubbd_kring_device *idev;
	unsigned long minor;

	if (!info || !info->ubbd_kring_dev)
		return;

	idev = info->ubbd_kring_dev;
	minor = idev->minor;

	mutex_lock(&idev->info_lock);

	idev->info = NULL;
	mutex_unlock(&idev->info_lock);

	wake_up_interruptible(&idev->wait);
	kill_fasync(&idev->async_queue, SIGIO, POLL_HUP);

	device_unregister(&idev->dev);

	ubbd_kring_free_minor(minor);

	return;
}
EXPORT_SYMBOL_GPL(ubbd_kring_unregister_device);

int ubbd_kring_init(void)
{
	return init_ubbd_kring_class();
}

void ubbd_kring_exit(void)
{
	release_ubbd_kring_class();
	idr_destroy(&ubbd_kring_idr);
}

/* setup ubbd kring */
static int ubbd_irqcontrol(struct ubbd_kring_info *info, s32 irq_on)
{
	struct ubbd_queue *ubbd_q = container_of(info, struct ubbd_queue, ubbd_kring_info);

	if (unlikely(atomic_read(&ubbd_q->status) == UBBD_QUEUE_KSTATUS_REMOVING)) {
		ubbd_queue_debug(ubbd_q, "is removing. dont queue complete_work.");
		return 0;
	}

	ubbd_queue_complete(ubbd_q);

	return 0;
}

static void ubbd_vma_open(struct vm_area_struct *vma)
{
	struct ubbd_queue *ubbd_q = vma->vm_private_data;
	struct ubbd_device *ubbd_dev = ubbd_q->ubbd_dev;

	ubbd_queue_debug(ubbd_q, "vma_open\n");
	ubbd_dev_get(ubbd_dev);
}

static void ubbd_vma_close(struct vm_area_struct *vma)
{
	struct ubbd_queue *ubbd_q = vma->vm_private_data;
	struct ubbd_device *ubbd_dev = ubbd_q->ubbd_dev;

	if (vma->vm_flags & VM_WRITE) {
		mutex_lock(&ubbd_q->state_lock);
		clear_bit(UBBD_QUEUE_FLAGS_HAS_BACKEND, &ubbd_q->flags);
		ubbd_q->backend_pid = 0;
		mutex_unlock(&ubbd_q->state_lock);
	}

	ubbd_queue_debug(ubbd_q, "vma_close\n");
	ubbd_dev_put(ubbd_dev);
}

static struct page *ubbd_try_get_data_page(struct ubbd_queue *ubbd_q, uint32_t dpi)
{
	struct page *page;

	page = xa_load(&ubbd_q->data_pages_array, dpi);
	if (unlikely(!page)) {
		ubbd_queue_debug(ubbd_q, "Invalid addr to data page mapping (dpi %u) on device %s\n",
		       dpi, ubbd_q->ubbd_dev->name);
		return NULL;
	}

	return page;
}

static vm_fault_t ubbd_vma_fault(struct vm_fault *vmf)
{
	struct ubbd_queue *ubbd_q = vmf->vma->vm_private_data;
	struct ubbd_kring_info *info = &ubbd_q->ubbd_kring_info;
	struct page *page;
	unsigned long offset;
	void *addr;

	offset = vmf->pgoff << PAGE_SHIFT;

	if (offset < ubbd_q->data_off) {
		addr = (void *)(unsigned long)info->mem[0].addr + offset;
		page = vmalloc_to_page(addr);
	} else {
		uint32_t dpi;

		dpi = (offset - ubbd_q->data_off) / PAGE_SIZE;
		page = ubbd_try_get_data_page(ubbd_q, dpi);
		if (!page)
			return VM_FAULT_SIGBUS;
		ubbd_queue_debug(ubbd_q, "ubbd ubbd_kring fault page: %p", page);
	}

	get_page(page);
	ubbd_queue_debug(ubbd_q, "ubbd ubbd_kring fault return page: %p", page);
	vmf->page = page;
	return 0;
}

static const struct vm_operations_struct ubbd_vm_ops = {
	.open = ubbd_vma_open,
	.close = ubbd_vma_close,
	.fault = ubbd_vma_fault,
};

static int ubbd_kring_dev_mmap(struct ubbd_kring_info *info, struct vm_area_struct *vma)
{
	struct ubbd_queue *ubbd_q = container_of(info, struct ubbd_queue, ubbd_kring_info);

#ifdef HAVE_VM_FLAGS_SET
	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
#else
	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
#endif /* HAVE_VM_FLAGS_SET */
	vma->vm_ops = &ubbd_vm_ops;
	vma->vm_private_data = ubbd_q;

	if (vma_pages(vma) != ubbd_q->mmap_pages)
		return -EINVAL;

	mutex_lock(&ubbd_q->state_lock);
	if (test_bit(UBBD_QUEUE_FLAGS_HAS_BACKEND, &ubbd_q->flags)) {
		mutex_unlock(&ubbd_q->state_lock);
		return -EBUSY;
	}
	set_bit(UBBD_QUEUE_FLAGS_HAS_BACKEND, &ubbd_q->flags);
	ubbd_q->backend_pid = current->pid;
	mutex_unlock(&ubbd_q->state_lock);

	ubbd_vma_open(vma);
	ubbd_queue_debug(ubbd_q, "ubbd_kring mmap by process: %d\n", current->pid);

	return 0;
}

static int ubbd_kring_dev_open(struct ubbd_kring_info *info, struct inode *inode)
{
	struct ubbd_queue *ubbd_q = container_of(info, struct ubbd_queue, ubbd_kring_info);

	ubbd_q->inode = inode;
	ubbd_queue_debug(ubbd_q, "ubbd_kring opened by process: %d\n", current->pid);

	return 0;
}

static int ubbd_kring_dev_release(struct ubbd_kring_info *info, struct inode *inode)
{
	struct ubbd_queue *ubbd_q = container_of(info, struct ubbd_queue, ubbd_kring_info);

	ubbd_queue_debug(ubbd_q, "ubbd_kring release by process: %d\n", current->pid);

	return 0;
}

int ubbd_queue_kring_init(struct ubbd_queue *ubbd_q)
{
	struct ubbd_kring_info *info;

	info = &ubbd_q->ubbd_kring_info;
	info->version = __stringify(UBBD_SB_VERSION);

	info->mem[0].name = "ubbd buffer";
	info->mem[0].addr = (phys_addr_t)(uintptr_t)ubbd_q->sb_addr;
	info->mem[0].size = ubbd_q->mmap_pages << PAGE_SHIFT;

	info->irqcontrol = ubbd_irqcontrol;

	info->mmap = ubbd_kring_dev_mmap;
	info->open = ubbd_kring_dev_open;
	info->release = ubbd_kring_dev_release;

	info->name = kasprintf(GFP_KERNEL, "ubbd%d-%d", ubbd_q->ubbd_dev->dev_id, ubbd_q->index);
	if (!info->name)
		return -ENOMEM;

	return ubbd_kring_register_device(ubbd_kring_root_device, info);
}

void ubbd_queue_kring_destroy(struct ubbd_queue *ubbd_q)
{
	struct ubbd_kring_info *info = &ubbd_q->ubbd_kring_info;

	kfree(info->name);
	ubbd_kring_unregister_device(info);
}

void ubbd_kring_unmap_range(struct ubbd_queue *ubbd_q,
		loff_t const holebegin, loff_t const holelen, int even_cows)
{
	unmap_mapping_range(ubbd_q->inode->i_mapping, holebegin, holelen, even_cows);
}
