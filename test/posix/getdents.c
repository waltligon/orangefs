#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "quicklist.h"
#define ERR_MAX 256

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


static int dirent_granularity = 4096;
static int use_direntplus = 0;
static int recurse = 0;
static int use_lite = 0;
/* whether to use 64 bit calls or 32 bit ones */
static int use_64 = 0;
static char path[256] = ".";
static int nlevels = 0, nobjects = 0, verbose = 0;

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

#define __NR_getdents_plus 319
#define __NR_getdents64_plus 320

#define __NR_getdents_plus_lite 323
#define __NR_getdents64_plus_lite 324

/* kernel's version of stat is different from glibc stat */
struct kernel_stat {
	unsigned long  st_dev;
	unsigned long  st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned long  st_rdev;
	unsigned long  st_size;
	unsigned long  st_blksize;
	unsigned long  st_blocks;
	unsigned long  st_atim;
	unsigned long  st_atim_nsec;
	unsigned long  st_mtim;
	unsigned long  st_mtim_nsec;
	unsigned long  st_ctim;
	unsigned long  st_ctim_nsec;
	unsigned long  __unused4;
	unsigned long  __unused5;
};

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

struct stat64_lite {
	unsigned long long	st_dev;
	unsigned char	__pad0[4];
	unsigned long	__st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;
	unsigned long	st_uid;
	unsigned long	st_gid;
	unsigned long long	st_rdev;
	unsigned char	__pad3[4];
	unsigned long  st_litemask;
	unsigned char  __pad5[4];
	long long	st_size;
	unsigned long	st_blksize;
	unsigned long	st_blocks;	/* Number 512-byte blocks allocated. */
	unsigned long	__pad4;		/* future possible st_blocks high bits */
	unsigned long	st_atim;
	unsigned long	st_atim_nsec;
	unsigned long	st_mtim;
	unsigned int	st_mtim_nsec;
	unsigned long	st_ctim;
	unsigned long	st_ctim_nsec;
	unsigned long long	st_ino;
};

/* stat64 of kernel matches stat64 of glibc! hence we could stick with the same */
struct dirent64_plus {
	struct stat64   dp_stat;
	int             dp_stat_err;
	struct dirent64 dp_dirent;
};

struct dirent64_plus_lite {
	struct stat64_lite   dp_stat_lite;
	int                  dp_stat_lite_err;
	struct dirent64 	   dp_dirent;
};

#elif defined (x86_64) || defined (__x86_64__)

#define __NR_newstatlite   275
#define __NR_newlstatlite  276
#define __NR_newfstatlite  277

#define __NR_getdents_plus 278
#define __NR_getdents64_plus 279

#define __NR_getdents_plus_lite 282
#define __NR_getdents64_plus_lite 283

/* kernel's version of stat could be different from glibc stat */
struct kernel_stat {
	unsigned long	st_dev;
	unsigned long	st_ino;
	unsigned long	st_nlink;

	unsigned int	st_mode;
	unsigned int	st_uid;
	unsigned int	st_gid;
	unsigned int	__pad0;
	unsigned long	st_rdev;
	long		st_size;
	long		st_blksize;
	long		st_blocks;	/* Number 512-byte blocks allocated. */
	unsigned long	st_atim;
	unsigned long 	st_atim_nsec; 
	unsigned long	st_mtim;
	unsigned long	st_mtim_nsec;
	unsigned long	st_ctim;
	unsigned long   st_ctim_nsec;
  	long		__unused[3];
};

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

#define stat64_lite kernel_stat_lite

/* stat64 of kernel is the same as kernel_stat on opteron 64 */
struct dirent64_plus {
	struct kernel_stat   dp_stat;
	int             dp_stat_err;
	struct dirent64 dp_dirent;
};

struct dirent64_plus_lite {
	struct kernel_stat_lite dp_stat_lite;
	int              dp_stat_lite_err;
	struct dirent64  dp_dirent;
};

#endif

struct dirent_plus {
	struct kernel_stat   dp_stat;
	int           dp_stat_err;
	struct dirent dp_dirent;
};

struct dirent_plus_lite {
	struct kernel_stat_lite dp_stat_lite;
	int              dp_stat_lite_err;
	struct dirent    dp_dirent;
};

static QLIST_HEAD(TREE);

/* Information about each file in the hierarchy */
struct files {
	/* Full path name of the file */
	char 				  					name[PATH_MAX];
	/* inode number of the file   */
	int64_t 			  					inode;
	int visited;
	/* The level field is used to link struct files both in the tree and in the flist */
	struct qlist_head 					level; 
};

static inline struct files* clone_filp(struct files *filp)
{
	struct files *new_filp = (struct files *)calloc(1, sizeof(struct files));
	if (new_filp) {
		memcpy(new_filp, filp, sizeof(*filp));
		/* Reset next and prev to NULL */
		new_filp->level.next = new_filp->level.prev = NULL;
	}
	return new_filp;
}

/* the _syscallXX apprroach is not portable. instead, we'll use syscall and
 * sadly forego any type checking.  For reference, here are the prototypes for
 * the system calls 
int newstatlite(const char *, struct kernel_stat_lite *);
static int newfstatlite(int, struct kernel_stat_lite *);
static int newlstatlite(const char *, struct kernel_stat_lite *);
static int getdents(uint, struct dirent *, uint);
static int getdents64(uint, struct dirent64 *, uint);
static int getdents_plus(uint, struct dirent_plus *, uint);
static int getdents64_plus(uint, struct dirent64_plus *, uint);
static int getdents_plus_lite(unsigned int fd, unsigned long lite_mask,
				struct dirent_plus_lite *dirent, unsigned int count);
static int getdents64_plus_lite(unsigned int fd, unsigned long lite_mask,
				struct dirent64_plus_lite *dirent, unsigned int count);
*/


/* free the struct files queued on the TREE list */

static void dealloc_treelist(void)
{
	while (TREE.next != &TREE) {
		struct files *filp;

		filp = qlist_entry(TREE.next, struct files, level);
		qlist_del(TREE.next);
		free(filp);
	}
	return;
}

/*
 * NOTES: Since we want BREADTH-FIRST TRAVERSAL, we build a link list(queue).
 * If we wanted DEPTH-FIRST TRAVERSAL, then we need to build a stack here, i.e
 * we need to use qlist_add() function instead of qlist_add_tail()
 */

static int path_init(const char *path)
{
	struct files *filp = NULL;

	filp = (struct files *)calloc(1, sizeof(struct files));
	if (!filp) {
		perror("do_root:calloc");
		return -1;
	}
	snprintf(filp->name, NAME_MAX + 1, "%s", path);
	/* add it to the tree of to-be visited nodes */
	qlist_add_tail(&filp->level, &TREE);
	filp->visited = 1;
	return 0;
}

/*
  build a string of a specified length that's either
  left or right justified based on the src string;
  caller must free ptr passed out as *out_str_p
*/
static inline void format_size_string(
    char *src_str, int num_spaces_total, char **out_str_p,
    int right_justified, int hard_limit)
{
    int len = 0;
    int spaces_size_allowed = 0;
    char *buf = NULL, *start = NULL, *src_start = NULL;

    assert(src_str);
    len = strlen(src_str);

    if (hard_limit)
    {
	spaces_size_allowed = (num_spaces_total ? num_spaces_total : len);
    }
    else
    {
	spaces_size_allowed = len;
	if (len < num_spaces_total)
        {
	    spaces_size_allowed = num_spaces_total;
        }
    }
	
    buf = (char *)malloc(spaces_size_allowed+1);
    assert(buf);

    memset(buf,0,spaces_size_allowed+1);

    if ((len > 0) && (len <= spaces_size_allowed))
    {
        memset(buf,' ',(spaces_size_allowed));

        src_start = src_str;

        if (right_justified)
        {
            start = &buf[(spaces_size_allowed-(len))];
        }
        else
        {
            start = buf;
        }

        while(src_start && (*src_start))
        {
            *start++ = *src_start++;
        }
        *out_str_p = strdup(buf);
    }
    else if (len > 0)
    {
        /* if the string is too long, don't format it */
	*out_str_p = strdup(src_str);
    }
    else if (len == 0)
    {
        *out_str_p = strdup("");
    }
    free(buf);
}

static void print_entry_stat64(
    char *entry_name,
    struct stat64 *attr,
	 const char *link_target)
{
    char buf[128] = {0}, *formatted_size = NULL;
    char *formatted_owner = NULL, *formatted_group = NULL;
    struct group *grp = NULL;
    struct passwd *pwd = NULL;
    char *empty_str = "";
    char *owner = empty_str, *group = empty_str;
    char *inode = empty_str;
    time_t mtime = (time_t)attr->st_mtime;
    struct tm *time = localtime(&mtime);
    unsigned long size = 0;
    char scratch_owner[16] = {0}, scratch_group[16] = {0};
    char scratch_size[16] = {0};
    char f_type = '-';
    char group_x_char = '-';

    if (attr == NULL)
    {
        return;
    }

    snprintf(scratch_owner,16,"%d",(int)attr->st_uid);
    snprintf(scratch_group,16,"%d",(int)attr->st_gid);

    if (S_ISREG(attr->st_mode))
    {
        size = attr->st_size;
    }
    else if (S_ISLNK(attr->st_mode))
    {
        size = 0;
    }
    else if (S_ISDIR(attr->st_mode))
    {
        size = 4096;
    }

	  snprintf(scratch_size,16, "%ld", size);
    format_size_string(scratch_size,11,&formatted_size,1,1);

	  owner = scratch_owner;
	  group = scratch_group;

            pwd = getpwuid((uid_t)attr->st_uid);
            owner = (pwd ? pwd->pw_name : scratch_owner);
            grp = getgrgid((gid_t)attr->st_gid);
            group = (grp ? grp->gr_name : scratch_group);

    /* for owner and group allow the fields to grow larger than 8 if
     * necessary (set hard_limit to 0), but pad anything smaller to
     * take up 8 spaces.
     */
    format_size_string(owner,8,&formatted_owner,0,0);
    format_size_string(group,8,&formatted_group,0,0);

    if (S_ISDIR(attr->st_mode))
    {
        f_type =  'd';
    }
    else if (S_ISLNK(attr->st_mode))
    {
        f_type =  'l';
    }

	 if (link_target == NULL)
    snprintf(buf,128,"%s%c%c%c%c%c%c%c%c%c%c    1 %s %s %s "
             "%.4d-%.2d-%.2d %.2d:%.2d %s",
             inode,
             f_type,
             ((attr->st_mode & S_IRUSR) ? 'r' : '-'),
             ((attr->st_mode & S_IWUSR) ? 'w' : '-'),
             ((attr->st_mode & S_IXUSR) ? 'x' : '-'),
             ((attr->st_mode & S_IRGRP) ? 'r' : '-'),
             ((attr->st_mode & S_IWGRP) ? 'w' : '-'),
             group_x_char,
             ((attr->st_mode & S_IROTH) ? 'r' : '-'),
             ((attr->st_mode & S_IWOTH) ? 'w' : '-'),
             ((attr->st_mode & S_IXOTH) ? 'x' : '-'),
             formatted_owner,
             formatted_group,
             formatted_size,
             (time->tm_year + 1900),
             (time->tm_mon + 1),
             time->tm_mday,
             (time->tm_hour),
             (time->tm_min),
             entry_name);
	 else 
		 snprintf(buf,128,"%s%c%c%c%c%c%c%c%c%c%c    1 %s %s %s "
					 "%.4d-%.2d-%.2d %.2d:%.2d %s -> %s",
					 inode,
					 f_type,
					 ((attr->st_mode & S_IRUSR) ? 'r' : '-'),
					 ((attr->st_mode & S_IWUSR) ? 'w' : '-'),
					 ((attr->st_mode & S_IXUSR) ? 'x' : '-'),
					 ((attr->st_mode & S_IRGRP) ? 'r' : '-'),
					 ((attr->st_mode & S_IWGRP) ? 'w' : '-'),
					 group_x_char,
					 ((attr->st_mode & S_IROTH) ? 'r' : '-'),
					 ((attr->st_mode & S_IWOTH) ? 'w' : '-'),
					 ((attr->st_mode & S_IXOTH) ? 'x' : '-'),
					 formatted_owner,
					 formatted_group,
					 formatted_size,
					 (time->tm_year + 1900),
					 (time->tm_mon + 1),
					 time->tm_mday,
					 (time->tm_hour),
					 (time->tm_min),
					 entry_name, link_target);


    if (formatted_size)
    {
        free(formatted_size);
    }
    if (formatted_owner)
    {
        free(formatted_owner);
    }
    if (formatted_group)
    {
        free(formatted_group);
    }
	 if ((use_direntplus == 0 && f_type != 'd') || use_direntplus != 0)
			  nobjects++;
	if (verbose)
	printf("%s\n",buf);
}

static void print_entry_stat(
    char *entry_name,
    struct stat *attr, const char *link_target)
{
    char buf[128] = {0}, *formatted_size = NULL;
    char *formatted_owner = NULL, *formatted_group = NULL;
    struct group *grp = NULL;
    struct passwd *pwd = NULL;
    char *empty_str = "";
    char *owner = empty_str, *group = empty_str;
    char *inode = empty_str;
    time_t mtime = (time_t)attr->st_mtime;
    struct tm *time = localtime(&mtime);
    unsigned long size = 0;
    char scratch_owner[16] = {0}, scratch_group[16] = {0};
    char scratch_size[16] = {0};
    char f_type = '-';
    char group_x_char = '-';

    if (attr == NULL)
    {
        return;
    }

    snprintf(scratch_owner,16,"%d",(int)attr->st_uid);
    snprintf(scratch_group,16,"%d",(int)attr->st_gid);

    if (S_ISREG(attr->st_mode))
    {
        size = attr->st_size;
    }
    else if (S_ISLNK(attr->st_mode))
    {
        size = 0;
    }
    else if (S_ISDIR(attr->st_mode))
    {
        size = 4096;
    }

	  snprintf(scratch_size,16, "%ld", size);
    format_size_string(scratch_size,11,&formatted_size,1,1);

	  owner = scratch_owner;
	  group = scratch_group;

            pwd = getpwuid((uid_t)attr->st_uid);
            owner = (pwd ? pwd->pw_name : scratch_owner);
            grp = getgrgid((gid_t)attr->st_gid);
            group = (grp ? grp->gr_name : scratch_group);

    /* for owner and group allow the fields to grow larger than 8 if
     * necessary (set hard_limit to 0), but pad anything smaller to
     * take up 8 spaces.
     */
    format_size_string(owner,8,&formatted_owner,0,0);
    format_size_string(group,8,&formatted_group,0,0);

    if (S_ISDIR(attr->st_mode))
    {
        f_type =  'd';
    }
    else if (S_ISLNK(attr->st_mode))
    {
        f_type =  'l';
    }

	 if (link_target == NULL)
    snprintf(buf,128,"%s%c%c%c%c%c%c%c%c%c%c    1 %s %s %s "
             "%.4d-%.2d-%.2d %.2d:%.2d %s",
             inode,
             f_type,
             ((attr->st_mode & S_IRUSR) ? 'r' : '-'),
             ((attr->st_mode & S_IWUSR) ? 'w' : '-'),
             ((attr->st_mode & S_IXUSR) ? 'x' : '-'),
             ((attr->st_mode & S_IRGRP) ? 'r' : '-'),
             ((attr->st_mode & S_IWGRP) ? 'w' : '-'),
             group_x_char,
             ((attr->st_mode & S_IROTH) ? 'r' : '-'),
             ((attr->st_mode & S_IWOTH) ? 'w' : '-'),
             ((attr->st_mode & S_IXOTH) ? 'x' : '-'),
             formatted_owner,
             formatted_group,
             formatted_size,
             (time->tm_year + 1900),
             (time->tm_mon + 1),
             time->tm_mday,
             (time->tm_hour),
             (time->tm_min),
             entry_name);
	 else
    snprintf(buf,128,"%s%c%c%c%c%c%c%c%c%c%c    1 %s %s %s "
             "%.4d-%.2d-%.2d %.2d:%.2d %s -> %s",
             inode,
             f_type,
             ((attr->st_mode & S_IRUSR) ? 'r' : '-'),
             ((attr->st_mode & S_IWUSR) ? 'w' : '-'),
             ((attr->st_mode & S_IXUSR) ? 'x' : '-'),
             ((attr->st_mode & S_IRGRP) ? 'r' : '-'),
             ((attr->st_mode & S_IWGRP) ? 'w' : '-'),
             group_x_char,
             ((attr->st_mode & S_IROTH) ? 'r' : '-'),
             ((attr->st_mode & S_IWOTH) ? 'w' : '-'),
             ((attr->st_mode & S_IXOTH) ? 'x' : '-'),
             formatted_owner,
             formatted_group,
             formatted_size,
             (time->tm_year + 1900),
             (time->tm_mon + 1),
             time->tm_mday,
             (time->tm_hour),
             (time->tm_min),
             entry_name, link_target);


    if (formatted_size)
    {
        free(formatted_size);
    }
    if (formatted_owner)
    {
        free(formatted_owner);
    }
    if (formatted_group)
    {
        free(formatted_group);
    }
	 if ((use_direntplus == 0 && f_type != 'd') || use_direntplus != 0)
			  nobjects++;
	if (verbose)
	 printf("%s\n",buf);
}

static void print_entry_kernel_stat(
    char *entry_name,
    struct kernel_stat *attr, const char *link_target)
{
    char buf[128] = {0}, *formatted_size = NULL;
    char *formatted_owner = NULL, *formatted_group = NULL;
    struct group *grp = NULL;
    struct passwd *pwd = NULL;
    char *empty_str = "";
    char *owner = empty_str, *group = empty_str;
    char *inode = empty_str;
    time_t mtime = (time_t)attr->st_mtim;
    struct tm *time = localtime(&mtime);
    unsigned long size = 0;
    char scratch_owner[16] = {0}, scratch_group[16] = {0};
    char scratch_size[16] = {0};
    char f_type = '-';
    char group_x_char = '-';

    if (attr == NULL)
    {
        return;
    }

    snprintf(scratch_owner,16,"%d",(int)attr->st_uid);
    snprintf(scratch_group,16,"%d",(int)attr->st_gid);

    if (S_ISREG(attr->st_mode))
    {
        size = attr->st_size;
    }
    else if (S_ISLNK(attr->st_mode))
    {
        size = 0;
    }
    else if (S_ISDIR(attr->st_mode))
    {
        size = 4096;
    }

	  snprintf(scratch_size,16, "%ld", size);
    format_size_string(scratch_size,11,&formatted_size,1,1);

	  owner = scratch_owner;
	  group = scratch_group;

            pwd = getpwuid((uid_t)attr->st_uid);
            owner = (pwd ? pwd->pw_name : scratch_owner);
            grp = getgrgid((gid_t)attr->st_gid);
            group = (grp ? grp->gr_name : scratch_group);

    /* for owner and group allow the fields to grow larger than 8 if
     * necessary (set hard_limit to 0), but pad anything smaller to
     * take up 8 spaces.
     */
    format_size_string(owner,8,&formatted_owner,0,0);
    format_size_string(group,8,&formatted_group,0,0);

    if (S_ISDIR(attr->st_mode))
    {
        f_type =  'd';
    }
    else if (S_ISLNK(attr->st_mode))
    {
        f_type =  'l';
    }

	 if (link_target == NULL)
    snprintf(buf,128,"%s%c%c%c%c%c%c%c%c%c%c    1 %s %s %s "
             "%.4d-%.2d-%.2d %.2d:%.2d %s",
             inode,
             f_type,
             ((attr->st_mode & S_IRUSR) ? 'r' : '-'),
             ((attr->st_mode & S_IWUSR) ? 'w' : '-'),
             ((attr->st_mode & S_IXUSR) ? 'x' : '-'),
             ((attr->st_mode & S_IRGRP) ? 'r' : '-'),
             ((attr->st_mode & S_IWGRP) ? 'w' : '-'),
             group_x_char,
             ((attr->st_mode & S_IROTH) ? 'r' : '-'),
             ((attr->st_mode & S_IWOTH) ? 'w' : '-'),
             ((attr->st_mode & S_IXOTH) ? 'x' : '-'),
             formatted_owner,
             formatted_group,
             formatted_size,
             (time->tm_year + 1900),
             (time->tm_mon + 1),
             time->tm_mday,
             (time->tm_hour),
             (time->tm_min),
             entry_name);
	 else
    snprintf(buf,128,"%s%c%c%c%c%c%c%c%c%c%c    1 %s %s %s "
             "%.4d-%.2d-%.2d %.2d:%.2d %s -> %s",
             inode,
             f_type,
             ((attr->st_mode & S_IRUSR) ? 'r' : '-'),
             ((attr->st_mode & S_IWUSR) ? 'w' : '-'),
             ((attr->st_mode & S_IXUSR) ? 'x' : '-'),
             ((attr->st_mode & S_IRGRP) ? 'r' : '-'),
             ((attr->st_mode & S_IWGRP) ? 'w' : '-'),
             group_x_char,
             ((attr->st_mode & S_IROTH) ? 'r' : '-'),
             ((attr->st_mode & S_IWOTH) ? 'w' : '-'),
             ((attr->st_mode & S_IXOTH) ? 'x' : '-'),
             formatted_owner,
             formatted_group,
             formatted_size,
             (time->tm_year + 1900),
             (time->tm_mon + 1),
             time->tm_mday,
             (time->tm_hour),
             (time->tm_min),
             entry_name, link_target);

    if (formatted_size)
    {
        free(formatted_size);
    }
    if (formatted_owner)
    {
        free(formatted_owner);
    }
    if (formatted_group)
    {
        free(formatted_group);
    }
	 if ((use_direntplus == 0 && f_type != 'd') || use_direntplus != 0)
			  nobjects++;
	if (verbose)
	printf("%s\n",buf);
}

static void print_entry_kernel_stat_lite(
    char *entry_name,
    struct kernel_stat_lite *attr, const char *link_target)
{
    char buf[128] = {0};
    char *formatted_owner = NULL, *formatted_group = NULL;
    struct group *grp = NULL;
    struct passwd *pwd = NULL;
    char *empty_str = "";
    char *owner = empty_str, *group = empty_str;
    char *inode = empty_str;
    time_t mtime = (time_t)attr->st_mtim;
    struct tm *time = localtime(&mtime);
    char scratch_owner[16] = {0}, scratch_group[16] = {0};
    char f_type = '-';
    char group_x_char = '-';

    if (attr == NULL)
    {
        return;
    }

    snprintf(scratch_owner,16,"%d",(int)attr->st_uid);
    snprintf(scratch_group,16,"%d",(int)attr->st_gid);

	  owner = scratch_owner;
	  group = scratch_group;

            pwd = getpwuid((uid_t)attr->st_uid);
            owner = (pwd ? pwd->pw_name : scratch_owner);
            grp = getgrgid((gid_t)attr->st_gid);
            group = (grp ? grp->gr_name : scratch_group);

    /* for owner and group allow the fields to grow larger than 8 if
     * necessary (set hard_limit to 0), but pad anything smaller to
     * take up 8 spaces.
     */
    format_size_string(owner,8,&formatted_owner,0,0);
    format_size_string(group,8,&formatted_group,0,0);

    if (S_ISDIR(attr->st_mode))
    {
        f_type =  'd';
    }
    else if (S_ISLNK(attr->st_mode))
    {
        f_type =  'l';
    }

	 if (link_target == NULL)
    snprintf(buf,128,"%s%c%c%c%c%c%c%c%c%c%c    1 %s %s "
             "%.4d-%.2d-%.2d %.2d:%.2d %s",
             inode,
             f_type,
             ((attr->st_mode & S_IRUSR) ? 'r' : '-'),
             ((attr->st_mode & S_IWUSR) ? 'w' : '-'),
             ((attr->st_mode & S_IXUSR) ? 'x' : '-'),
             ((attr->st_mode & S_IRGRP) ? 'r' : '-'),
             ((attr->st_mode & S_IWGRP) ? 'w' : '-'),
             group_x_char,
             ((attr->st_mode & S_IROTH) ? 'r' : '-'),
             ((attr->st_mode & S_IWOTH) ? 'w' : '-'),
             ((attr->st_mode & S_IXOTH) ? 'x' : '-'),
             formatted_owner,
             formatted_group,
             (time->tm_year + 1900),
             (time->tm_mon + 1),
             time->tm_mday,
             (time->tm_hour),
             (time->tm_min),
             entry_name);
	 else
    snprintf(buf,128,"%s%c%c%c%c%c%c%c%c%c%c    1 %s %s "
             "%.4d-%.2d-%.2d %.2d:%.2d %s -> %s",
             inode,
             f_type,
             ((attr->st_mode & S_IRUSR) ? 'r' : '-'),
             ((attr->st_mode & S_IWUSR) ? 'w' : '-'),
             ((attr->st_mode & S_IXUSR) ? 'x' : '-'),
             ((attr->st_mode & S_IRGRP) ? 'r' : '-'),
             ((attr->st_mode & S_IWGRP) ? 'w' : '-'),
             group_x_char,
             ((attr->st_mode & S_IROTH) ? 'r' : '-'),
             ((attr->st_mode & S_IWOTH) ? 'w' : '-'),
             ((attr->st_mode & S_IXOTH) ? 'x' : '-'),
             formatted_owner,
             formatted_group,
             (time->tm_year + 1900),
             (time->tm_mon + 1),
             time->tm_mday,
             (time->tm_hour),
             (time->tm_min),
             entry_name, link_target);

    if (formatted_owner)
    {
        free(formatted_owner);
    }
    if (formatted_group)
    {
        free(formatted_group);
    }
	 if ((use_direntplus == 0 && f_type != 'd') || use_direntplus != 0)
				nobjects++;
    if (verbose)
	  printf("%s\n",buf);
}

static void print_entry_stat64_lite(
    char *entry_name,
    struct stat64_lite *attr, const char *link_target)
{
    char buf[128] = {0};
    char *formatted_owner = NULL, *formatted_group = NULL;
    struct group *grp = NULL;
    struct passwd *pwd = NULL;
    char *empty_str = "";
    char *owner = empty_str, *group = empty_str;
    char *inode = empty_str;
    time_t mtime = (time_t)attr->st_mtim;
    struct tm *time = localtime(&mtime);
    char scratch_owner[16] = {0}, scratch_group[16] = {0};
    char f_type = '-';
    char group_x_char = '-';

    if (attr == NULL)
    {
        return;
    }

    snprintf(scratch_owner,16,"%d",(int)attr->st_uid);
    snprintf(scratch_group,16,"%d",(int)attr->st_gid);

	  owner = scratch_owner;
	  group = scratch_group;

            pwd = getpwuid((uid_t)attr->st_uid);
            owner = (pwd ? pwd->pw_name : scratch_owner);
            grp = getgrgid((gid_t)attr->st_gid);
            group = (grp ? grp->gr_name : scratch_group);

    /* for owner and group allow the fields to grow larger than 8 if
     * necessary (set hard_limit to 0), but pad anything smaller to
     * take up 8 spaces.
     */
    format_size_string(owner,8,&formatted_owner,0,0);
    format_size_string(group,8,&formatted_group,0,0);

    if (S_ISDIR(attr->st_mode))
    {
        f_type =  'd';
    }
    else if (S_ISLNK(attr->st_mode))
    {
        f_type =  'l';
    }

	 if (link_target == NULL)
    snprintf(buf,128,"%s%c%c%c%c%c%c%c%c%c%c    1 %s %s "
             "%.4d-%.2d-%.2d %.2d:%.2d %s",
             inode,
             f_type,
             ((attr->st_mode & S_IRUSR) ? 'r' : '-'),
             ((attr->st_mode & S_IWUSR) ? 'w' : '-'),
             ((attr->st_mode & S_IXUSR) ? 'x' : '-'),
             ((attr->st_mode & S_IRGRP) ? 'r' : '-'),
             ((attr->st_mode & S_IWGRP) ? 'w' : '-'),
             group_x_char,
             ((attr->st_mode & S_IROTH) ? 'r' : '-'),
             ((attr->st_mode & S_IWOTH) ? 'w' : '-'),
             ((attr->st_mode & S_IXOTH) ? 'x' : '-'),
             formatted_owner,
             formatted_group,
             (time->tm_year + 1900),
             (time->tm_mon + 1),
             time->tm_mday,
             (time->tm_hour),
             (time->tm_min),
             entry_name);
	 else
    snprintf(buf,128,"%s%c%c%c%c%c%c%c%c%c%c    1 %s %s "
             "%.4d-%.2d-%.2d %.2d:%.2d %s -> %s",
             inode,
             f_type,
             ((attr->st_mode & S_IRUSR) ? 'r' : '-'),
             ((attr->st_mode & S_IWUSR) ? 'w' : '-'),
             ((attr->st_mode & S_IXUSR) ? 'x' : '-'),
             ((attr->st_mode & S_IRGRP) ? 'r' : '-'),
             ((attr->st_mode & S_IWGRP) ? 'w' : '-'),
             group_x_char,
             ((attr->st_mode & S_IROTH) ? 'r' : '-'),
             ((attr->st_mode & S_IWOTH) ? 'w' : '-'),
             ((attr->st_mode & S_IXOTH) ? 'x' : '-'),
             formatted_owner,
             formatted_group,
             (time->tm_year + 1900),
             (time->tm_mon + 1),
             time->tm_mday,
             (time->tm_hour),
             (time->tm_min),
             entry_name, link_target);

    if (formatted_owner)
    {
        free(formatted_owner);
    }
    if (formatted_group)
    {
        free(formatted_group);
    }
	 if ((use_direntplus == 0 && f_type != 'd') || use_direntplus != 0)
				nobjects++;
    if (verbose)
	  printf("%s\n",buf);
}


/*
 * Notes: We don't follow symbolic links, and we ignore any special
 * files like sockets, pipes etc that may be residing on this file
 * system. Hence, the hierarchy will be a directed acyclic tree!.
 * Thus, the traversal becomes much simpler than that of a complex graph.
 */
static int path_walk(struct files *root_filp)
{
	int dir_fd, ret;
	struct files *filp;
	struct stat statbuf;
	struct stat64 stat64buf;
	struct kernel_stat_lite statbuflite;
	static int invoke_count = 0;
	int is_dir, is_link;
	char link_target[NAME_MAX], *lnk = NULL;

	invoke_count++;
	ret = 0;
	memset(&statbuf, 0, sizeof(statbuf));
	/* Dequeue from the list of to-be-visited nodes */
	qlist_del(&root_filp->level);

	dir_fd = open(root_filp->name, O_RDONLY | O_NOFOLLOW);
	
	if (dir_fd < 0) {
		/* lets just stat the file here, it might be some sort 
		 * of stale or special file or symbolic link 
		 */
		if (use_64) 
		{
			if (use_lite)
			{
				statbuflite.st_litemask = S_SLITE_ATIME | 
										 S_SLITE_MTIME |
										 S_SLITE_CTIME |
										 S_SLITE_BLKSIZE |
										 S_SLITE_BLOCKS;
				ret = syscall(__NR_newlstatlite, root_filp->name, &statbuflite);
				is_dir = S_ISDIR(statbuflite.st_mode);
				is_link = S_ISLNK(statbuflite.st_mode);
			}
			else
			{
				ret = lstat64(root_filp->name, &stat64buf);
				is_dir = S_ISDIR(stat64buf.st_mode);
				is_link = S_ISLNK(stat64buf.st_mode);
			}
		}
		else {
			if (use_lite)
			{
				statbuflite.st_litemask = S_SLITE_ATIME | 
										 S_SLITE_MTIME |
										 S_SLITE_CTIME |
										 S_SLITE_BLKSIZE |
										 S_SLITE_BLOCKS;
				ret = syscall(__NR_newlstatlite, root_filp->name, &statbuflite);
				is_dir = S_ISDIR(statbuflite.st_mode);
				is_link = S_ISLNK(statbuflite.st_mode);
			}
			else 
			{
				ret = lstat(root_filp->name, &statbuf);
				is_dir = S_ISDIR(statbuf.st_mode);
				is_link = S_ISLNK(statbuf.st_mode);
			}
		}
	}
	else {
		if (use_64) 
		{
			if (use_lite)
			{
				statbuflite.st_litemask = S_SLITE_ATIME | 
										 S_SLITE_MTIME |
										 S_SLITE_CTIME |
										 S_SLITE_BLKSIZE |
										 S_SLITE_BLOCKS;
				ret = syscall(__NR_newfstatlite,dir_fd, &statbuflite);
				is_dir = S_ISDIR(statbuflite.st_mode);
				is_link = S_ISLNK(statbuflite.st_mode);
			}
			else
			{
				ret = fstat64(dir_fd, &stat64buf);
				is_dir = S_ISDIR(stat64buf.st_mode);
				is_link = S_ISLNK(stat64buf.st_mode);
			}
		}
		else 
		{
			if (use_lite)
			{
				statbuflite.st_litemask = S_SLITE_ATIME | 
										 S_SLITE_MTIME |
										 S_SLITE_CTIME |
										 S_SLITE_BLKSIZE |
										 S_SLITE_BLOCKS;
				ret = syscall(__NR_newfstatlite, dir_fd, &statbuflite);
				is_dir = S_ISDIR(statbuflite.st_mode);
				is_link = S_ISLNK(statbuflite.st_mode);
			}
			else
			{
				ret = fstat(dir_fd, &statbuf);
				is_dir = S_ISDIR(statbuf.st_mode);
				is_link = S_ISLNK(statbuf.st_mode);
			}
		}
	}
	if (ret < 0)
		goto err;
	nlevels = root_filp->visited;
	/* Are we looking at a directory? */
	if (is_dir) 
	{
		if (use_direntplus == 0)
			  nobjects++;
		/* Use plain old getdents interface */
		if (dir_fd >= 0 && use_direntplus == 0) 
		{
			struct dirent64 *p64 = NULL, *next64;
			struct dirent *p = NULL, *next;
			int dirent_input_count = dirent_granularity, counter = 1;
			long total_rec_len, i, dirent_total_count = 0, dirent_output_bytes, dirent_total_bytes = 0;
			char *ptr = NULL;

			if (invoke_count > 1 && recurse == 0) {
				ret = 0;
				if (use_64) {
					if (use_lite)
						print_entry_kernel_stat_lite(root_filp->name, &statbuflite, NULL);
					else
						print_entry_stat64(root_filp->name, &stat64buf, NULL);
				}
				else {
					if (use_lite)
						print_entry_kernel_stat_lite(root_filp->name, &statbuflite, NULL);
					else
						print_entry_stat(root_filp->name, &statbuf, NULL);
				}
				goto err;
			}

			lseek(dir_fd, 0, SEEK_SET);
			do {
				if (use_64)
				{
					if (dirent_total_bytes >= (dirent_input_count * (counter - 1) * sizeof(struct dirent64)))
					{
						p64 = (struct dirent64 *) realloc(p64, 
								(dirent_input_count * counter++) * sizeof(struct dirent64));
						assert(p64);
					}
					ptr = (char *) p64 + dirent_total_bytes;
					dirent_output_bytes = syscall(__NR_getdents64, dir_fd, 
										 (struct dirent64 *) ptr, 
										 dirent_input_count * sizeof(struct dirent64));
					if (dirent_output_bytes <= 0)
						break;
					total_rec_len = 0;
					dirent_total_bytes += dirent_output_bytes;
					while (total_rec_len < dirent_output_bytes) {
						next64 = (struct dirent64 *) (ptr + total_rec_len);
						total_rec_len += next64->d_reclen;
						dirent_total_count++;
					}
				}
				else {
					if (dirent_total_bytes >= (dirent_input_count * (counter - 1) * sizeof(struct dirent)))
					{
						p = (struct dirent *) realloc(p, 
								(dirent_input_count * counter++) * sizeof(struct dirent));
						assert(p);
					}
					ptr = (char *) p + dirent_total_bytes;
					dirent_output_bytes = syscall(__NR_getdents, dir_fd, 
										 (struct dirent *) ptr, 
										 dirent_input_count * sizeof(struct dirent));
					if (dirent_output_bytes <= 0)
						break;
					total_rec_len = 0;
					dirent_total_bytes += dirent_output_bytes;
					while (total_rec_len < dirent_output_bytes) {
						next = (struct dirent *) (ptr + total_rec_len);
						total_rec_len += next->d_reclen;
						dirent_total_count++;
					}
				}
			} while (1);
			if (dirent_output_bytes < 0) {
				perror("getdents/getdents64");
				goto err;
			}
			if (use_64)
				ptr = (char *) p64;
			else
				ptr = (char *) p;
			total_rec_len = 0;
			for (i = 0; i < dirent_total_count; i++) 
			{
				if (use_64)
				{
					next64 = (struct dirent64 *) (ptr + total_rec_len);
					total_rec_len += next64->d_reclen;
					/* As long as it is not . or .. recurse */
					if (strcmp(next64->d_name, ".") && strcmp(next64->d_name, "..")) 
					{
						filp = (struct files *)calloc(1, sizeof(struct files));
						if (!filp) {
							perror("do_root:calloc");
							return -1;
						}
						snprintf(filp->name, NAME_MAX + 1, "%s/%s", root_filp->name, next64->d_name);
						/* Add to the tree */
						qlist_add_tail(&filp->level, &TREE);
						filp->visited = root_filp->visited + 1;
					}
				}
				else {
					next = (struct dirent *) (ptr + total_rec_len);
					total_rec_len += next->d_reclen;
					/* As long as it is not . or .. recurse */
					if (strcmp(next->d_name, ".") && strcmp(next->d_name, "..")) 
					{
						filp = (struct files *)calloc(1, sizeof(struct files));
						if (!filp) {
							perror("do_root:calloc");
							return -1;
						}
						snprintf(filp->name, NAME_MAX + 1, "%s/%s", root_filp->name, next->d_name);
						/* Add to the tree */
						qlist_add_tail(&filp->level, &TREE);
						filp->visited = root_filp->visited + 1;
					}
				}
			}
			if (use_64)
				free(p64);
			else
				free(p);
		}
		/* Use the new fangled 64 bit interface */
		else if (dir_fd >= 0 && use_direntplus == 1)
		{
			struct dirent64_plus *p64 = NULL, *next64;
			struct dirent_plus *p = NULL, *next = NULL;
			int dirent_input_count = dirent_granularity, counter = 1;
			int total_rec_len, i, dirent_total_count = 0, dirent_output_bytes, dirent_total_bytes = 0;
			char *ptr = NULL, fname[NAME_MAX];
			struct dirent_plus_lite *p_lite = NULL, *next_lite = NULL;
			struct dirent64_plus_lite *p64_lite = NULL, *next64_lite = NULL;
			unsigned long lite_mask = S_SLITE_ATIME | S_SLITE_MTIME | S_SLITE_CTIME | S_SLITE_BLKSIZE | S_SLITE_BLOCKS;

			lseek(dir_fd, 0, SEEK_SET);
			do {
				if (use_64)
				{
					if (use_lite)
					{
						if (dirent_total_bytes >= (dirent_input_count * (counter - 1) * sizeof(struct dirent64_plus_lite)))
						{
							p64_lite = (struct dirent64_plus_lite *) realloc(p64_lite, 
									(dirent_input_count * counter++) * sizeof(struct dirent64_plus_lite));
							assert(p64_lite);
						}
						ptr = (char *) p64_lite + dirent_total_bytes;
						dirent_output_bytes = syscall(__NR_getdents64_plus_lite, 
											 dir_fd, lite_mask, 
											 (struct dirent64_plus_lite *) ptr, 
											 dirent_input_count * sizeof(struct dirent64_plus_lite));
						if (dirent_output_bytes <= 0)
							break;
						total_rec_len = 0;
						dirent_total_bytes += dirent_output_bytes;
						while (total_rec_len < dirent_output_bytes) {
							next64_lite = (struct dirent64_plus_lite *) (ptr + total_rec_len);
							total_rec_len += next64_lite->dp_dirent.d_reclen;
							dirent_total_count++;
						}
					}
					else 
					{
						if (dirent_total_bytes >= (dirent_input_count * (counter - 1) * sizeof(struct dirent64_plus)))
						{
							p64 = (struct dirent64_plus *) realloc(p64, 
									(dirent_input_count * counter++) * sizeof(struct dirent64_plus));
							assert(p64);
						}
						ptr = (char *) p64 + dirent_total_bytes;
						dirent_output_bytes = syscall(__NR_getdents64_plus, dir_fd, 
											 (struct dirent64_plus *) ptr, 
											 dirent_input_count * sizeof(struct dirent64_plus));
						if (dirent_output_bytes <= 0)
							break;
						total_rec_len = 0;
						dirent_total_bytes += dirent_output_bytes;
						while (total_rec_len < dirent_output_bytes) {
							next64 = (struct dirent64_plus *) (ptr + total_rec_len);
							total_rec_len += next64->dp_dirent.d_reclen;
							dirent_total_count++;
						}
					}
				}
				else 
				{
					if (use_lite)
					{
						if (dirent_total_bytes >= (dirent_input_count * (counter - 1) * sizeof(struct dirent_plus_lite)))
						{
							p_lite = (struct dirent_plus_lite *) realloc(p_lite,
									(dirent_input_count * counter++) * sizeof(struct dirent_plus_lite));
							assert(p_lite);
						}
						ptr = (char *) p_lite + dirent_total_bytes;
						dirent_output_bytes = syscall(__NR_getdents_plus_lite, 
											 dir_fd, lite_mask, 
											 (struct dirent_plus_lite *) ptr, 
											 dirent_input_count * sizeof(struct dirent_plus_lite));
						if (dirent_output_bytes <= 0)
							break;
						total_rec_len = 0;
						dirent_total_bytes += dirent_output_bytes;
						while (total_rec_len < dirent_output_bytes) {
							next_lite = (struct dirent_plus_lite *) (ptr + total_rec_len);
							total_rec_len += next_lite->dp_dirent.d_reclen;
							dirent_total_count++;
						}
					}
					else
					{
						if (dirent_total_bytes >= (dirent_input_count * (counter - 1) * sizeof(struct dirent_plus)))
						{
							p = (struct dirent_plus *) realloc(p,
									(dirent_input_count * counter++) * sizeof(struct dirent_plus));
							assert(p);
						}
						ptr = (char *) p + dirent_total_bytes;
						dirent_output_bytes = syscall(__NR_getdents_plus, dir_fd, 
								(struct dirent_plus *) ptr, 
								dirent_input_count * sizeof(struct dirent_plus));
						if (dirent_output_bytes <= 0)
							break;
						total_rec_len = 0;
						dirent_total_bytes += dirent_output_bytes;
						while (total_rec_len < dirent_output_bytes) {
							next = (struct dirent_plus *) (ptr + total_rec_len);
							total_rec_len += next->dp_dirent.d_reclen;
							dirent_total_count++;
						}
					}
				}
			} while (1);
			if (dirent_output_bytes < 0) {
				perror("getdents_plus/getdents64_plus");
				goto err;
			}
			if (use_64)
				ptr = (use_lite) ? (char *) p64_lite : (char *) p64;
			else
			{
				ptr = (use_lite) ? (char *) p_lite : (char *) p;
			}
			total_rec_len = 0;
			for (i = 0; i < dirent_total_count; i++) 
			{
				if (use_64)
				{
					if (use_lite)
					{
						next64_lite = (struct dirent64_plus_lite *) (ptr + total_rec_len);
						total_rec_len += next64_lite->dp_dirent.d_reclen;
						/* As long as it is not . or .. recurse */
						if (strcmp(next64_lite->dp_dirent.d_name, ".") && strcmp(next64_lite->dp_dirent.d_name, "..")) 
						{
							lnk = NULL;
							snprintf(fname, NAME_MAX, "%s/%s", root_filp->name, next64_lite->dp_dirent.d_name);
							if (S_ISLNK(next64_lite->dp_stat_lite.st_mode))
							{
								readlink(fname, link_target, NAME_MAX);
								lnk = link_target;
							}
							print_entry_stat64_lite(fname, &next64_lite->dp_stat_lite, lnk);
							if (recurse && S_ISDIR(next64_lite->dp_stat_lite.st_mode))
							{
								filp = (struct files *)calloc(1, sizeof(struct files));
								if (!filp) {
									perror("do_root:calloc");
									return -1;
								}
								snprintf(filp->name, NAME_MAX + 1, "%s/%s", root_filp->name, next64_lite->dp_dirent.d_name);
								/* Add to the tree */
								qlist_add_tail(&filp->level, &TREE);
								filp->visited = root_filp->visited + 1;
							}
						}
					}
					else
					{
						next64 = (struct dirent64_plus *) (ptr + total_rec_len);
						total_rec_len += next64->dp_dirent.d_reclen;
						/* As long as it is not . or .. recurse */
						if (strcmp(next64->dp_dirent.d_name, ".") && strcmp(next64->dp_dirent.d_name, "..")) 
						{
							lnk = NULL;
							snprintf(fname, NAME_MAX, "%s/%s", root_filp->name, next64->dp_dirent.d_name);
							if (S_ISLNK(next64->dp_stat.st_mode))
							{
								readlink(fname, link_target, NAME_MAX);
								lnk = link_target;
							}
#if defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
							print_entry_stat64(fname, &next64->dp_stat, lnk);
#elif defined (x86_64) || defined (__x86_64__)
							print_entry_kernel_stat(fname, &next64->dp_stat, lnk);
#endif
							if (recurse && S_ISDIR(next64->dp_stat.st_mode))
							{
								filp = (struct files *)calloc(1, sizeof(struct files));
								if (!filp) {
									perror("do_root:calloc");
									return -1;
								}
								snprintf(filp->name, NAME_MAX + 1, "%s/%s", root_filp->name, next64->dp_dirent.d_name);
								/* Add to the tree */
								qlist_add_tail(&filp->level, &TREE);
								filp->visited = root_filp->visited + 1;
							}
						}
					}
				}
				else 
				{
					if (use_lite)
					{
						next_lite = (struct dirent_plus_lite *) (ptr + total_rec_len);
						total_rec_len += next_lite->dp_dirent.d_reclen;
						/* As long as it is not . or .. recurse */
						if (strcmp(next_lite->dp_dirent.d_name, ".") && strcmp(next_lite->dp_dirent.d_name, "..")) 
						{
							lnk = NULL;
							snprintf(fname, NAME_MAX, "%s/%s", root_filp->name, next_lite->dp_dirent.d_name);
							if (S_ISLNK(next_lite->dp_stat_lite.st_mode))
							{
								readlink(fname, link_target, NAME_MAX);
								lnk = link_target;
							}
							print_entry_kernel_stat_lite(fname, &next_lite->dp_stat_lite, lnk);
							if (recurse && S_ISDIR(next_lite->dp_stat_lite.st_mode))
							{
								filp = (struct files *)calloc(1, sizeof(struct files));
								if (!filp) {
									perror("do_root:calloc");
									return -1;
								}
								snprintf(filp->name, NAME_MAX + 1, "%s/%s", root_filp->name, next_lite->dp_dirent.d_name);
								/* Add to the tree */
								qlist_add_tail(&filp->level, &TREE);
								filp->visited = root_filp->visited + 1;
							}
						}
					}
					else
					{
						next = (struct dirent_plus *) (ptr + total_rec_len);
						total_rec_len += next->dp_dirent.d_reclen;
						/* As long as it is not . or .. recurse */
						if (strcmp(next->dp_dirent.d_name, ".") && strcmp(next->dp_dirent.d_name, "..")) 
						{
							lnk = NULL;
							snprintf(fname, NAME_MAX, "%s/%s", root_filp->name, next->dp_dirent.d_name);
							if (S_ISLNK(next->dp_stat.st_mode))
							{
								readlink(fname, link_target, NAME_MAX);
								lnk = link_target;
							}
							print_entry_kernel_stat(fname, &next->dp_stat, lnk);
							if (recurse && S_ISDIR(next->dp_stat.st_mode))
							{
								filp = (struct files *)calloc(1, sizeof(struct files));
								if (!filp) {
									perror("do_root:calloc");
									return -1;
								}
								snprintf(filp->name, NAME_MAX + 1, "%s/%s", root_filp->name, next->dp_dirent.d_name);
								/* Add to the tree */
								qlist_add_tail(&filp->level, &TREE);
								filp->visited = root_filp->visited + 1;
							}
						}
					}
				}
			}
			if (use_64)
			{
				if (use_lite)
					free(p64_lite);
				else
					free(p64);
			}
			else
			{
				if (use_lite)
					free(p_lite);
				else
					free(p);
			}
		}
	}
	/* or are we looking at a regular file? */
	else {
		if (use_direntplus == 0) {
			if (use_64) {
				lnk = NULL;
				if (is_link)
				{
					readlink(root_filp->name, link_target, NAME_MAX);
					lnk = link_target;
				}
				if (use_lite)
					print_entry_kernel_stat_lite(root_filp->name, &statbuflite, lnk);
				else
					print_entry_stat64(root_filp->name, &stat64buf, lnk);
			}
			else {
				lnk = NULL;
				if (is_link)
				{
					readlink(root_filp->name, link_target, NAME_MAX);
					lnk = link_target;
				}
				if (use_lite)
					print_entry_kernel_stat_lite(root_filp->name, &statbuflite, lnk);
				else
					print_entry_stat(root_filp->name, &statbuf, lnk);
			}
		}
	} 
err:
	/* Ignore any symbolic links, device-files, any other special files */
	free(root_filp);
	if (dir_fd > 0) close(dir_fd);
	return ret;
}

static double Wtime(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return((double)t.tv_sec + (double)(t.tv_usec) * 1e-06);
}

static int do_flatten_hierarchy(void)
{
	struct files *filp = NULL;
	double begin, end;

	begin = Wtime();
	/* traverse the tree and prune out unnecessary files and flatten the hierarchy */
	path_init(path);
	/* walk the tree */
	while (TREE.next != &TREE) 
	{
		filp = (struct files *) qlist_entry(TREE.next, struct files, level);
		/* Visit the node */
		if (path_walk(filp) < 0) {
			perror("do_flatten_hierarchy: path_walk:");
			return -1;
		}
	}
	end = Wtime();
	if (use_direntplus)
	{
			printf("getdents_plus (lite %s) ( %d levels %d objects) took %g secs\n",
								use_lite ? "yes" : "no", 
								nlevels, nobjects, (end - begin));
	}
	else
	{
			printf("getdents (lite %s) ( %d levels %d objects) took %g secs\n",
								use_lite ? "yes" : "no",
								nlevels - 1, nobjects - 1, (end - begin));
	}
	return 0;
}

static void usage(char *str)
{
	fprintf(stderr, "Usage: %s -f <directory/file path> -n <dirent read granularity> -r {recurse} -s {use 64 bit dirent calls} -p {use direntplus interface} -v {verbose} -h {this help message}\n", str);
	return;
}

int main(int argc, char *argv[])
{
	char c;

	while ((c = getopt(argc, argv, "n:f:rsphlv")) != EOF)
	{
		switch(c) {
			case 'l':
				use_lite = 1;
				break;
		   case 'v':
		      verbose = 1;
		      break;
			case 's':
				use_64 = 1;
				break;
			case 'r':
				recurse = 1;
				break;
			case 'f':
				strcpy(path, optarg);
				break;
			case 'p':
				use_direntplus = 1;
				break;
			case 'n':
				dirent_granularity = atoi(optarg);
				break;
			case 'h':
			case '?':
			default:
				fprintf(stderr, "Unknown option\n");
				usage(argv[0]);
				exit(1);
		}
	}
	do_flatten_hierarchy();
	dealloc_treelist();
	return 0;
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
