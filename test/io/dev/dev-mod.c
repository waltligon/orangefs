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
#include <asm/uaccess.h>

#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

EXPORT_NO_SYMBOLS;

ssize_t	pdev_read(struct file *, char *, size_t, loff_t *);
ssize_t	pdev_write(struct file *, const char *, size_t, loff_t *);
int	pdev_open(struct inode *, struct file *);
int	pdev_release(struct inode *, struct file *);
int	pdev_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
static int __init pdev_init(void);
static void __exit pdev_exit(void);

static struct file_operations pdev_fops = {
    read:		pdev_read,
    write:		pdev_write,
    open:		pdev_open,
    release:	pdev_release,
    ioctl:		pdev_ioctl,
};

static int pdev_major = 0;

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
    printk("pdev: pdev_open()\n");
    return(-ENOSYS);
}

int pdev_release(struct inode *inode, struct file *filp)
{
    printk("pdev: pdev_release()\n");
    return(-ENOSYS);
}

int pdev_ioctl(struct inode *inode, 
		  struct file *flip,
		  unsigned int command, 
		  unsigned long arg)
{
    printk("pdev: pdev_ioctl()\n");
    return(-ENOSYS);
} 

ssize_t	pdev_read(struct file *filp, char * buf, 
		 size_t size, loff_t *offp)
{
    printk("pdev: pdev_read()\n");
    return(-ENOSYS);
}

ssize_t	pdev_write(struct file *filp, const char *buf, 
		  size_t size, loff_t *offp)
{
    printk("pdev: pdev_write()\n");
    return(-ENOSYS);
}

module_init(pdev_init);
module_exit(pdev_exit);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
