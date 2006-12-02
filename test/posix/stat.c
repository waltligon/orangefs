#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/unistd.h>
#include "quicklist.h"
#define ERR_MAX 256

static int dirent_granularity = 4096;
static int recurse = 0;
static char path[256] = ".";
static int use_lite = 0;
static int nobjects = 0, nlevels = 0, verbose = 0;
static double total = 0.0;

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

static QLIST_HEAD(TREE);
static double Wtime(void);

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

static void print_entry_stat(
    char *entry_name,
    struct stat *attr, const char *link_target)
{
    char buf[128] = {0};
    char *formatted_owner = NULL, *formatted_group = NULL;
    struct group *grp = NULL;
    struct passwd *pwd = NULL;
    char *empty_str = "";
    char *owner = empty_str, *group = empty_str;
    char *inode = empty_str;
    time_t mtime = (time_t)attr->st_mtime;
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
    nobjects++;
    if (verbose)
	  printf("%s\n",buf);
}

/* the _syscallXX apprroach is not portable. instead, we'll use syscall and
 * sadly forego any type checking.  For reference, here are the prototypes for
 * the system calls 
int newstatlite(const char *, struct kernel_stat_lite *);
static int newfstatlite(int, struct kernel_stat_lite *);
static int newlstatlite(const char *, struct kernel_stat_lite *);
static int getdents(uint, struct dirent *, uint);
*/

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
	struct kernel_stat_lite statbuflite;
	static int invoke_count = 0;
	int is_dir, is_link;
	char link_target[NAME_MAX], *lnk = NULL;
	double begin, end;

	invoke_count++;
	ret = 0;
	memset(&statbuf, 0, sizeof(statbuf));
	/* Dequeue from the list of to-be-visited nodes */
	qlist_del(&root_filp->level);

	dir_fd = open(root_filp->name, O_RDONLY | O_NOFOLLOW);
	if (dir_fd < 0) 
	{
		/* lets just statlite the file here, it might be some sort 
		 * of stale or special file or symbolic link 
		 */
		begin = Wtime();
		if (use_lite) {
			statbuflite.st_litemask = S_SLITE_ATIME | 
										 S_SLITE_MTIME |
										 S_SLITE_CTIME |
										 S_SLITE_BLKSIZE |
										 S_SLITE_BLOCKS;
			ret = syscall(__NR_newlstatlite, root_filp->name, 
					&statbuflite);
			is_dir = S_ISDIR(statbuflite.st_mode);
			is_link = S_ISLNK(statbuflite.st_mode);
		}
		else {
			ret = lstat(root_filp->name, &statbuf);
			is_dir = S_ISDIR(statbuf.st_mode);
			is_link = S_ISLNK(statbuf.st_mode);
		}
		end = Wtime();
		total += (end - begin);
	}
	else {
		begin = Wtime();
		if (use_lite) {
			statbuflite.st_litemask = S_SLITE_ATIME | 
										 S_SLITE_MTIME |
										 S_SLITE_CTIME |
										 S_SLITE_BLKSIZE |
										 S_SLITE_BLOCKS;
			ret = syscall(__NR_newfstatlite, dir_fd, &statbuflite);
			is_dir = S_ISDIR(statbuflite.st_mode);
			is_link = S_ISLNK(statbuflite.st_mode);
		}
		else {
			ret = fstat(dir_fd, &statbuf);
			is_dir = S_ISDIR(statbuf.st_mode);
			is_link = S_ISLNK(statbuf.st_mode);
		}
		end = Wtime();
		total += (end - begin);
	}
	if (ret < 0)
		goto err;
	nlevels = root_filp->visited;
	/* Are we looking at a directory? */
	if (is_dir) 
	{
		nobjects++;
		/* Use plain old getdents interface */
		if (dir_fd >= 0) 
		{
			struct dirent *p = NULL, *next;
			int dirent_input_count = dirent_granularity, counter = 1;
			long total_rec_len, i, dirent_total_count = 0, dirent_output_bytes, dirent_total_bytes = 0;
			char *ptr = NULL;

			if (invoke_count > 1 && recurse == 0) {
				ret = 0;
				if (use_lite)
					print_entry_kernel_stat_lite(root_filp->name, &statbuflite,
							NULL);
				else
					print_entry_stat(root_filp->name, &statbuf, NULL);
				goto err;
			}
			lseek(dir_fd, 0, SEEK_SET);
			do {
					if (dirent_total_bytes >= (dirent_input_count * (counter - 1) * sizeof(struct dirent)))
					{
						p = (struct dirent *) realloc(p, 
								(dirent_input_count * counter++) * sizeof(struct dirent));
						assert(p);
					}
					ptr = (char *) p + dirent_total_bytes;
					dirent_output_bytes = syscall(__NR_getdents,
							dir_fd, 
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
			} while(1);
			if (dirent_output_bytes < 0) {
				perror("getdents/getdents64");
				goto err;
			}
			ptr = (char *) p;
			total_rec_len = 0;
			for (i = 0; i < dirent_total_count; i++) 
			{
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
			free(p);
		}
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

	/* traverse the tree and prune out unnecessary files and flatten the hierarchy */
	path_init(path);
	/* walk the tree */
	while (TREE.next != &TREE) {
		filp = (struct files *) qlist_entry(TREE.next, struct files, level);
		/* Visit the node */
		if (path_walk(filp) < 0) {
			perror("do_flatten_hierarchy: path_walk:");
			return -1;
		}
	}
	if (use_lite) 
		printf("statlite ( %d levels %d objects) took %g secs\n",
				nlevels - 1, nobjects - 1, total);
	else 
		printf("stat ( %d levels %d objects) took %g secs\n",
				nlevels - 1, nobjects - 1, total);
	return 0;
}

static void usage(char *str)
{
	fprintf(stderr, "Usage: %s -f <directory/file path> -n <dirent read "
			"granularity> -r {recurse} -v {verbose} -l {use stat lite} -h {this help message}\n",
			str);

	return;
}

int main(int argc, char *argv[])
{
	char c;

	while ((c = getopt(argc, argv, "n:f:lrhv")) != EOF)
	{
		switch(c) {
			case 'v':
				verbose = 1;
				break;
			case 'l':
				use_lite = 1;
				break;
			case 'r':
				recurse = 1;
				break;
			case 'f':
				strcpy(path, optarg);
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
