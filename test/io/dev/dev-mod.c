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

static int __init pdev_init(void)
{
//    register_chrdev(0, "pdev", &pdev_fops);
    printk("PDEV: init\n");
    return 0;
}

static void __exit pdev_exit(void)
{
    printk("PDEV: exit\n");
    return;
}

int pdev_open(struct inode *inode, struct file *filp)
{
    printk("pdev: pdev_ioctl()\n");
    return(-ENOSYS);
}

int pdev_release(struct inode *inode, struct file *filp)
{
    printk("pdev: pdev_ioctl()\n");
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
    printk("pdev: pdev_read()\n");
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
