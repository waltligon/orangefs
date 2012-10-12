/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* this is just a trivial module that provides a character device for 
 * testing the pvfs2/src/io/dev/ code
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/smp_lock.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include "pint-dev-shared.h"

#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

EXPORT_NO_SYMBOLS;

ssize_t	pdev_read(struct file *, char *, size_t, loff_t *);
ssize_t	pdev_writev(struct file *, const struct iovec *, unsigned long, 
    loff_t *);
int	pdev_open(struct inode *, struct file *);
int	pdev_release(struct inode *, struct file *);
int	pdev_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
unsigned int pdev_poll(struct file *my_file, struct poll_table_struct *wait);
static int __init pdev_init(void);
static void __exit pdev_exit(void);

static struct file_operations pdev_fops = {
    read:		pdev_read,
    writev:		pdev_writev,
    open:		pdev_open,
    release:	   	pdev_release,
    ioctl:		pdev_ioctl,
    poll:		pdev_poll,
};

static int pdev_major = 0;
/* note: just picking these arbitrarily for testing */
int32_t pdev_magic = 1234;
int32_t pdev_max_upsize = 1024;
int32_t pdev_max_downsize = 1024;

static int __init pdev_init(void)
{

    int ret;
   
#ifdef HAVE_DEVFS
    if ((ret = devfs_register_chrdev(0, "pvfs2-req", &pdev_fops)) < 0)
#else
    if ((ret = register_chrdev(0, "pvfs2-req", &pdev_fops)) < 0)
#endif
    {
	printk("PDEV: failed to assign major number\n");
	return -ENODEV;
    }

    pdev_major = ret;

#ifdef HAVE_DEVFS
    devfs_handle = devfs_register(NULL, "pvfs2-req", DEVFS_FL_DEFAULT,
	pdev_major, 0, (S_IFCHR | S_IRUSR | S_IWUSR), &pdev_fops, NULL);
#endif

    printk("PDEV: init successful\n");

    return 0;
}

static void __exit pdev_exit(void)
{

#ifdef HAVE_DEVFS
    devfs_unregister(devfs_handle);
    devfs_unregister_chrdev(pdev_major, "pvfs2-req");
#else
    unregister_chrdev(pdev_major, "pvfs2-req");
#endif

    printk("PDEV: exit successful\n");
    return;
}

int pdev_open(struct inode *inode, struct file *filp)
{
    printk("PDEV: pdev_open()\n");
    MOD_INC_USE_COUNT;
    return(0);
}

int pdev_release(struct inode *inode, struct file *filp)
{
    printk("PDEV: pdev_release()\n");
    MOD_DEC_USE_COUNT;
    return(0);
}

int pdev_ioctl(struct inode *inode, 
		  struct file *flip,
		  unsigned int command, 
		  unsigned long arg)
{
    printk("PDEV: pdev_ioctl()\n");

    switch(command)
    {
	case(PVFS_DEV_GET_MAGIC):
	    copy_to_user((void*)arg, &pdev_magic, sizeof(int32_t)); 
	    return(0);
	case(PVFS_DEV_GET_MAX_UPSIZE):
	    copy_to_user((void*)arg, &pdev_max_upsize, sizeof(int32_t)); 
	    return(0);
	case(PVFS_DEV_GET_MAX_DOWNSIZE):
	    copy_to_user((void*)arg, &pdev_max_downsize, sizeof(int32_t)); 
	    return(0);
	default:
	    return(-ENOSYS);
    }

    return(-ENOSYS);
} 

ssize_t	pdev_read(struct file *filp, char * buf, size_t size, loff_t *offp)
{
    char test_string[] = "Hello world.";
    int64_t test_tag = 5;
    void* tmp_buf = buf;

    printk("PDEV: pdev_read()\n");

    if(size < (strlen(test_string) + 1 + sizeof(int32_t) + sizeof(int64_t)))
    {
	return(-EMSGSIZE);
    }

    /* copy out magic number */
    copy_to_user(tmp_buf, &pdev_magic, sizeof(int32_t));
    tmp_buf = (void*)((unsigned long)tmp_buf + sizeof(int32_t));
    /* copy out tag */
    copy_to_user(tmp_buf, &test_tag, sizeof(int64_t));
    tmp_buf = (void*)((unsigned long)tmp_buf + sizeof(int64_t));
    /* copy out message payload */
    copy_to_user(tmp_buf, test_string, (strlen(test_string) + 1));

    return(strlen(test_string) + 1 + sizeof(int32_t) + sizeof(int64_t));
}

ssize_t	pdev_writev(struct file *filp, const struct iovec* iov, 
    unsigned long count, loff_t *offp)
{
    void* buffer = NULL;
    void* offset;
    int remaining = pdev_max_downsize;
    int i;
    int32_t* magic;
    int64_t* tag;
    char* payload = NULL;

    printk("PDEV: pdev_writev()\n");

    /* for simplicity's sake, lets always dump to a contiguous buffer and 
     * then pull the message apart- logic is a bit complicated for handling 
     * iovec directly 
     */
    buffer = kmalloc(pdev_max_downsize, GFP_KERNEL);
    if(!buffer)
	return(-ENOMEM);

    offset = buffer;
    for(i=0; i<count; i++)
    {
	if(iov[i].iov_len > remaining)
	    return(-EMSGSIZE);
	copy_from_user(offset, iov[i].iov_base, iov[i].iov_len);
	remaining -= iov[i].iov_len;
	offset = (void*)((unsigned long)offset + iov[i].iov_len);
    }

    /* now, lets see what we got */
    magic = (int32_t*)buffer;
    tag = (int64_t*)((unsigned long)buffer + sizeof(int32_t));
    payload = (char*)((unsigned long)tag + sizeof(int64_t));

    if(*magic != pdev_magic)
	return(-EINVAL);

    printk("PDEV: magic: %d, tag: %d, payload: %s\n", (int)*magic, (int)*tag, 
	payload);

    kfree(buffer);

    return(pdev_max_downsize - remaining);
}

unsigned int pdev_poll(struct file *my_file, struct 
    poll_table_struct *wait){

    /* the test module is always ready for IO ... */
    return(POLLOUT|POLLWRNORM|POLLIN|POLLRDNORM);
}

module_init(pdev_init);
module_exit(pdev_exit);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
