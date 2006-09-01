#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/types.h>
#include <assert.h>
#include <linux/dirent.h>
#include <linux/unistd.h>

#define S_SLITE_SIZET     0x1
#define S_SLITE_BLKSIZE   0x2
#define S_SLITE_BLOCKS    0x4
#define S_SLITE_ATIME     0x8
#define S_SLITE_MTIME     0x10
#define S_SLITE_CTIME     0x20
#define S_SLITE_ALL       (S_SLITE_SIZET | S_SLITE_BLKSIZE | S_SLITE_BLOCKS \
									S_SLITE_ATIME | S_SLITE_MTIME   | S_SLITE_CTIME)

#define SLITE_SIZET(m)    ((m) & S_SLITE_SIZET)
#define SLITE_BLKSIZE(m)  ((m) & S_SLITE_BLKSIZE)
#define SLITE_BLOCKS(m)   ((m) & S_SLITE_BLOCKS)
#define SLITE_ATIME(m)    ((m) & S_SLITE_ATIME)
#define SLITE_MTIME(m)    ((m) & S_SLITE_MTIME)
#define SLITE_CTIME(m)    ((m) & S_SLITE_CTIME)

#if defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
/* FIXME:
 * PLEASE CHANGE THIS SYSTEM
 * CALL NUMBER IN CASE YOUR
 * ARCHITECTURE IS NOT IA-32 
 * OR IF YOUR KERNEL SYSCALL NUMBERS
 * ARE DIFFERENT. YOU HAVE BEEN WARNED!!!!!
 */
#define __NR_newstatlite 313
#define __NR_newlstatlite 314
#define __NR_newfstatlite 315

struct kernel_stat_lite {
	unsigned long  st_dev;
	unsigned long  st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned long  st_rdev;
	unsigned long  st_litemask;
	unsigned long  st_size;
	unsigned long  st_blksize;
	unsigned long  st_blocks;
	unsigned long  st_atim;
	unsigned long  st_atime_nsec;
	unsigned long  st_mtim;
	unsigned long  st_mtime_nsec;
	unsigned long  st_ctim;
	unsigned long  st_ctime_nsec;
	unsigned long  __unused4;
	unsigned long  __unused5;
};

#elif defined (x86_64) || defined (__x86_64__)

#define __NR_newstatlite   275
#define __NR_newlstatlite  276
#define __NR_newfstatlite  277

struct kernel_stat_lite {
	unsigned long	st_dev;
	unsigned long	st_ino;
	unsigned long	st_nlink;

	unsigned int	st_mode;
	unsigned int	st_uid;
	unsigned int	st_gid;
	unsigned int	__pad0;
	unsigned long	st_rdev;
	unsigned long  st_litemask;
	long		st_size;
	long		st_blksize;
	long		st_blocks;	/* Number 512-byte blocks allocated. */

	unsigned long	st_atim;
	unsigned long 	st_atime_nsec; 
	unsigned long	st_mtim;
	unsigned long	st_mtime_nsec;
	unsigned long	st_ctim;
	unsigned long   st_ctime_nsec;
  	long		__unused[3];
};
#endif

static int newstatlite(const char *, struct kernel_stat_lite *);
static int newfstatlite(int, struct kernel_stat_lite *);
static int newlstatlite(const char *, struct kernel_stat_lite *);
static int getdents(uint, struct dirent *, uint);

_syscall2(int, newstatlite, const char *, path, struct kernel_stat_lite *, buf);
_syscall2(int, newfstatlite, int, filedes, struct kernel_stat_lite *, buf);
_syscall2(int, newlstatlite, const char *, path, struct kernel_stat_lite *, buf);

#if !defined(_FILE_OFFSET_BITS) && !defined(_LARGEFILE64_SOURCE)
static void copy_statlite_to_stat(struct kernel_stat_lite *slbuf,
		struct stat *sbuf)
{
	sbuf->st_dev = slbuf->st_dev;
	sbuf->st_ino = slbuf->st_ino;
	sbuf->st_mode = slbuf->st_mode;
	sbuf->st_nlink = slbuf->st_nlink;
	sbuf->st_uid = slbuf->st_uid;
	sbuf->st_gid = slbuf->st_gid;
	sbuf->st_rdev = slbuf->st_rdev;
	sbuf->st_size = 0;
	sbuf->st_blksize = slbuf->st_blksize;
	sbuf->st_blocks = slbuf->st_blocks;
	sbuf->st_atime = slbuf->st_atim;
	sbuf->st_mtime = slbuf->st_mtim;
	sbuf->st_ctime = slbuf->st_ctim;
}

int __xstat(int vers, const char *pathname, struct stat *sbuf)
{
	struct kernel_stat_lite slbuf;
	int ret;
	memset(&slbuf, 0, sizeof(slbuf));
	slbuf.st_litemask = S_SLITE_ATIME | 
		S_SLITE_MTIME |
		S_SLITE_CTIME | 
		S_SLITE_BLKSIZE |
		S_SLITE_BLOCKS;
	ret = newstatlite(pathname, &slbuf);
	if (ret < 0)
		return ret;
	copy_statlite_to_stat(&slbuf, sbuf);
	return 0;
}

int __lxstat(int vers, const char *pathname, struct stat *sbuf)
{
	struct kernel_stat_lite slbuf;
	int ret;
	memset(&slbuf, 0, sizeof(slbuf));
	slbuf.st_litemask = S_SLITE_ATIME | 
		S_SLITE_MTIME |
		S_SLITE_CTIME | 
		S_SLITE_BLKSIZE |
		S_SLITE_BLOCKS;
	ret = newlstatlite(pathname, &slbuf);
	if (ret < 0)
		return ret;
	copy_statlite_to_stat(&slbuf, sbuf);
	return 0;
}

int __fxstat(int vers, int fd, struct stat *sbuf)
{
	struct kernel_stat_lite slbuf;
	int ret;
	memset(&slbuf, 0, sizeof(slbuf));
	slbuf.st_litemask = S_SLITE_ATIME | 
		S_SLITE_MTIME |
		S_SLITE_CTIME | 
		S_SLITE_BLKSIZE |
		S_SLITE_BLOCKS;
	ret = newfstatlite(fd, &slbuf);
	if (ret < 0)
		return ret;
	copy_statlite_to_stat(&slbuf, sbuf);
	return 0;
}
#else
static void copy_statlite_to_stat64(struct kernel_stat_lite *slbuf,
		struct stat64 *sbuf)
{
	sbuf->st_dev = slbuf->st_dev;
	sbuf->st_ino = slbuf->st_ino;
	sbuf->st_mode = slbuf->st_mode;
	sbuf->st_nlink = slbuf->st_nlink;
	sbuf->st_uid = slbuf->st_uid;
	sbuf->st_gid = slbuf->st_gid;
	sbuf->st_rdev = slbuf->st_rdev;
	sbuf->st_size = 0;
	sbuf->st_blksize = slbuf->st_blksize;
	sbuf->st_blocks = slbuf->st_blocks;
	sbuf->st_atime = slbuf->st_atim;
	sbuf->st_mtime = slbuf->st_mtim;
	sbuf->st_ctime = slbuf->st_ctim;
}

int __xstat64(int vers, const char *pathname, struct stat64 *sbuf)
{
	struct kernel_stat_lite slbuf;
	int ret;

	memset(&slbuf, 0, sizeof(slbuf));
	slbuf.st_litemask = S_SLITE_ATIME | 
		S_SLITE_MTIME |
		S_SLITE_CTIME | 
		S_SLITE_BLKSIZE |
		S_SLITE_BLOCKS;
	ret = newstatlite(pathname, &slbuf);
	if (ret < 0)
		return ret;
	copy_statlite_to_stat64(&slbuf, sbuf);
	return 0;
}

int __lxstat64(int vers, const char *pathname, struct stat64 *sbuf)
{
	struct kernel_stat_lite slbuf;
	int ret;

	memset(&slbuf, 0, sizeof(slbuf));
	slbuf.st_litemask = S_SLITE_ATIME | 
		S_SLITE_MTIME |
		S_SLITE_CTIME | 
		S_SLITE_BLKSIZE |
		S_SLITE_BLOCKS;
	ret = newlstatlite(pathname, &slbuf);
	if (ret < 0)
		return ret;
	copy_statlite_to_stat64(&slbuf, sbuf);
	return 0;
}

int __fxstat64(int vers, int fd, struct stat64 *sbuf)
{
	struct kernel_stat_lite slbuf;
	int ret;

	memset(&slbuf, 0, sizeof(slbuf));
	slbuf.st_litemask = S_SLITE_ATIME | 
		S_SLITE_MTIME |
		S_SLITE_CTIME | 
		S_SLITE_BLKSIZE |
		S_SLITE_BLOCKS;
	ret = newfstatlite(fd, &slbuf);
	if (ret < 0)
		return ret;
	copy_statlite_to_stat64(&slbuf, sbuf);
	return 0;
}
#endif
