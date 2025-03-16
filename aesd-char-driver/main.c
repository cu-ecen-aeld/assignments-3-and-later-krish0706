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
#include <linux/mutex.h>
#include <linux/fs.h> // file_operations
#include "aesd-circular-buffer.h"
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("krish0706"); 
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

// function prototypes
int aesd_open (struct inode *inode, struct file *filp);
int aesd_release (struct inode *inode, struct file *filp);
ssize_t aesd_read (struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write (struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
int aesd_init_module (void);
void aesd_cleanup_module (void);

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open\n");
    struct aesd_dev * p_dev;

    // store p_dev in private_data for use in other methods
    p_dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = p_dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release\n");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev * p_dev = filp->private_data;
    struct aesd_buffer_entry * p_entry = NULL;
    ssize_t retval = 0;
    size_t offset = 0;

    PDEBUG("read %zu bytes with offset %lld\n",count,*f_pos);

     // acquire mutex
    if (mutex_lock_interruptible(&p_dev->lock))
    {
        return -ERESTARTSYS;
    }

    // find entry corresponding to given offset
    if ((p_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&p_dev->circular_buffer, *f_pos, &offset)) == NULL)
    {
        goto end;
    }

    // check how many bytes are available in the buffer after the offset
    size_t bytes_available = p_entry->size - offset;

    // if buffer has more bytes than requested, limit number of bytes to count, else
    // limit it the number of bytes available
    size_t bytes_to_copy = (bytes_available > count) ? count : bytes_available;

    if (copy_to_user(buf, &p_entry->buffptr[offset], bytes_to_copy))
    {
        goto end;
    }

    *f_pos += bytes_to_copy;
    retval = bytes_to_copy;

    end: 
        // release mutex
        mutex_unlock(&p_dev->lock);
        return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    PDEBUG("write %zu bytes with offset %lld\n",count,*f_pos);
    struct aesd_dev * p_dev = filp->private_data;
    ssize_t retval = -ENOMEM;

     // acquire mutex
    if (mutex_lock_interruptible(&p_dev->lock))
    {
        return -ERESTARTSYS;
    }

    // realloc to create enough space for the incoming data, if write_buffer_size is 0,
    // this will act as malloc
    void * p_tmp = krealloc(p_dev->p_write_buffer, count + p_dev->write_buffer_size, GFP_KERNEL);
    if (p_tmp == NULL)
    {
        kfree(p_dev->p_write_buffer);
        retval = -ENOMEM;
        goto end;
    }
    else
    {
        p_dev->p_write_buffer = p_tmp;
    }

    // copy data to buffer at correct index to not overwrite previous data
    if (copy_from_user(p_dev->p_write_buffer + p_dev->write_buffer_size, buf, count))
    {
        retval = -EFAULT;
        goto end;
    }
    else
    {
        // all bytes copied successfully from user space
        retval = count;
    }

    // update write_buffer_size
    p_dev->write_buffer_size += count;

    // check if the write_buffer contains a \n
    int b_contains_newline = (memchr(p_dev->p_write_buffer, '\n', p_dev->write_buffer_size) != NULL) ? 1 : 0;

    if (b_contains_newline)
    {
        struct aesd_buffer_entry entry = {.buffptr=p_dev->p_write_buffer, .size=p_dev->write_buffer_size};
        if (p_dev->circular_buffer.full)
        {
            // if buffer is full, free oldest data before adding
            PDEBUG("buffer is full, deleing oldest entry");
            int out_offs = p_dev->circular_buffer.out_offs;
            kfree(p_dev->circular_buffer.entry[out_offs].buffptr);
        }

        PDEBUG("adding entry to buffer");
        aesd_circular_buffer_add_entry(&p_dev->circular_buffer, &entry);

        // reset write buffer variables 
        p_dev->write_buffer_size = 0;
        p_dev->p_write_buffer = NULL;
    }
    
    end: 
        // release mutex
        mutex_unlock(&p_dev->lock);
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
        printk(KERN_ERR "Error %d adding aesd cdev\n", err);
    }
    return err;
}

int __init aesd_init_module(void)
{
    PDEBUG("init\n");
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
    aesd_circular_buffer_init(&aesd_device.circular_buffer); 
    mutex_init(&aesd_device.lock);
    result = aesd_setup_cdev(&aesd_device);
    if( result ) {
        unregister_chrdev_region(dev, 1); 
    }
    return result;

}

module_init(aesd_init_module);

void __exit aesd_cleanup_module(void)
{
    PDEBUG("cleanup\n");
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    int index;
    struct aesd_buffer_entry *entry;

    // free allocated buffers
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer,index) {
        kfree(entry->buffptr);
    }

    cdev_del(&aesd_device.cdev);
    unregister_chrdev_region(devno, 1);
}

module_exit(aesd_cleanup_module);
