/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include "pvfs2-kernel.h"

/* this file implements the /dev/pvfs2-flow device node */

static int pvfs2_devflow_open(
    struct inode *inode,
    struct file *file)
{
    pvfs2_print("pvfs2: pvfs2_devflow_open called on %s (inode is %p)\n",
		file->f_dentry->d_name.name, inode);
    return generic_file_open(inode, file);
}

static ssize_t pvfs2_devflow_read(
    struct file *file,
    char *buf,
    size_t count,
    loff_t * offset)
{
    pvfs2_print("Someone's reading from the devflow file...\n");
    return 0;
}

static ssize_t pvfs2_devflow_write(
    struct file *file,
    const char *buf,
    size_t count,
    loff_t * offset)
{
    pvfs2_print("...Someone's writing to the devflow file\n");
    return 0;
}

/*
  NOTE: gets called when this device file has no more open references
*/
static int pvfs2_devflow_release(
    struct inode *inode,
    struct file *file)
{
    pvfs2_print("pvfs2: pvfs2_devflow_release called\n");
    return 0;
}

struct file_operations pvfs2_devflow_file_operations = {
    .read = pvfs2_devflow_read,
    .write = pvfs2_devflow_write,
    .open = pvfs2_devflow_open,
    .release = pvfs2_devflow_release,
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
