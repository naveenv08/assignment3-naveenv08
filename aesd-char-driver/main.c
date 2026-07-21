/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Your Name Here"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    PDEBUG("open");

    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);

    filp->private_data = dev;

    return 0;
}


int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");

    filp->private_data = NULL;

    return 0;
}


ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;

    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset;
    size_t bytes_to_copy;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    
    if (mutex_lock_interruptible(&dev->lock))
    return -ERESTARTSYS;

    /* Locate the buffer entry corresponding to the current file position */
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(
                &dev->buffer,
                *f_pos,
                &entry_offset);

if (entry)
    printk(KERN_INFO
           "READ: f_pos=%lld entry_size=%zu data=%s",
           *f_pos,
           entry->size,
           entry->buffptr);
else
    printk(KERN_INFO
           "READ: f_pos=%lld entry=NULL",
           *f_pos);

    if (!entry) {
        retval = 0;
        goto out;
    }

    /* Copy at most 'count' bytes */
    bytes_to_copy = min(count, entry->size - entry_offset);

    if (copy_to_user(buf,
                     entry->buffptr + entry_offset,
                     bytes_to_copy)) {
        retval = -EFAULT;
        goto out;
    }

    /* Advance file position */
    *f_pos += bytes_to_copy;

    retval = bytes_to_copy;

    out:
    mutex_unlock(&dev->lock);    


    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;

    struct aesd_dev *dev = filp->private_data;
    char *new_buffer;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

	if (mutex_lock_interruptible(&dev->lock))
	return -ERESTARTSYS;

	/* Extend (or allocate) the temporary write buffer */
	new_buffer = krealloc(dev->entry.buffptr, dev->entry.size + count, GFP_KERNEL);

	if (!new_buffer)
	goto out;

	/* Update the buffer pointer */
	dev->entry.buffptr = new_buffer;

	/* Copy the new data from userspace */	
	if (copy_from_user((char *)dev->entry.buffptr + dev->entry.size, buf, count)) {
		retval = -EFAULT;
		goto out;
	}

	/* Update the accumulated size */
	dev->entry.size += count;

	/* Successfully copied data */
	retval = count;

    /* If a complete command has been received, add it to the circular buffer */
    if (memchr(dev->entry.buffptr, '\n', dev->entry.size) != NULL) {

	aesd_circular_buffer_add_entry(&dev->buffer, &dev->entry);

        /* Start a fresh entry for the next command */
        dev->entry.buffptr = NULL;
        dev->entry.size = 0;
    }

	out:
	mutex_unlock(&dev->lock);

	return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }

    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */

	aesd_circular_buffer_init(&aesd_device.buffer);

	aesd_device.entry.buffptr = NULL;
	aesd_device.entry.size = 0;

	mutex_init(&aesd_device.lock);

	result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    struct aesd_buffer_entry *entry;
    uint8_t index;

    cdev_del(&aesd_device.cdev);

    /* Free all buffers stored in the circular buffer */
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        kfree((void *)entry->buffptr);
    }

    /* Free any partially accumulated write */
    kfree((void *)aesd_device.entry.buffptr);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
