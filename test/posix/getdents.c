#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "list.h"
#define ERR_MAX 256

static int use_direntplus = 0;
static int recurse = 0;
static char path[256] = ".";

#if defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
/* FIXME:
 * PLEASE CHANGE THIS SYSTEM
 * CALL NUMBER IN CASE YOUR
 * ARCHITECTURE IS NOT IA-32 
 * OR IF YOUR KERNEL SYSCALL NUMBERS
 * ARE DIFFERENT. YOU HAVE BEEN WARNED!!!!!
 */
#define __NR_getdents_plus 319
#define __NR_getdents64_plus 320
#elif defined (x86_64) || defined (__x86_64__)
#define __NR_getdents_plus 278
#define __NR_getdents64_plus 279
#endif

struct dirent_plus {
	struct stat   dp_stat;
	int           dp_stat_err;
	struct dirent dp_dirent;
};

struct dirent64_plus {
	struct stat64   dp_stat;
	int             dp_stat_err;
	struct dirent64 dp_dirent;
};

#define DIRENT_OFFSET(de) ((unsigned long) &((de)->dp_dirent) - (unsigned long) (de))
#define NAME_OFFSET(de) ((unsigned long) ((de)->d_name - (unsigned long) (de)))
#define ROUND_UP(x) (((x)+sizeof(long)-1) & ~(sizeof(long)-1))

#define reclen  ROUND_UP(NAME_OFFSET(de) + DIRENT_OFFSET(dirent) + 2)
#define reclen64  ROUND_UP(NAME_OFFSET(de64) + DIRENT_OFFSET(dirent64) + 2)

static LIST_HEAD(TREE);

/* Information about each file in the hierarchy */
struct files {
	/* Full path name of the file */
	char 				  					name[PATH_MAX];
	/* inode number of the file   */
	int64_t 			  					inode;
	/* The level field is used to link struct files both in the tree and in the flist */
	struct list_head 					level; 
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

/* glibc does not seem to provide a getdents() routine, so we provide one */
_syscall3(int, getdents, uint, fd, struct dirent *, dirp, uint, count);
_syscall3(int, getdents64, uint, fd, struct dirent64 *, dirp, uint, count);
_syscall3(int, getdents_plus, uint, fd, struct dirent_plus *, dirp, uint, count);
_syscall3(int, getdents64_plus, uint, fd, struct dirent64_plus *, dirp, uint, count);

/* free the struct files queued on the TREE list */

static void dealloc_treelist(void)
{
	while (TREE.next != &TREE) {
		struct files *filp;

		filp = list_entry(TREE.next, struct files, level);
		list_del(TREE.next);
		free(filp);
	}
	return;
}

/*
 * NOTES: Since we want BREADTH-FIRST TRAVERSAL, we build a link list(queue).
 * If we wanted DEPTH-FIRST TRAVERSAL, then we need to build a stack here, i.e
 * we need to use list_add() function instead of list_add_tail()
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
	list_add_tail(&filp->level, &TREE);
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

void print_entry_attr(
    char *entry_name,
    struct stat64 *attr)
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
	  printf("%s\n",buf);
}

void print_entry_attr2(
    char *entry_name,
    struct stat *attr)
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
	static int invoke_count = 0;

	invoke_count++;
	ret = 0;
	memset(&statbuf, 0, sizeof(statbuf));
	/* Dequeue from the list of to-be-visited nodes */
	list_del(&root_filp->level);

	dir_fd = open(root_filp->name, O_RDONLY | O_NOFOLLOW);
	
	if (dir_fd < 0) {
		/* lets just stat the file here, it might be some sort of stale or special file or symbolic link */
		ret = lstat(root_filp->name, &statbuf);
	}
	else {
		ret = fstat(dir_fd, &statbuf);
	}
	if (ret < 0)
		goto err;
	/* Are we looking at a directory? */
	if (S_ISDIR(statbuf.st_mode)) 
	{
		if (dir_fd >= 0 && use_direntplus == 0) 
		{
			struct dirent64 *p = NULL, *next;
			int dirent_input_count = 4096, counter = 1;
			int total_rec_len, i, dirent_total_count = 0, dirent_output_bytes, dirent_total_bytes = 0;
			char *ptr = NULL;

			if (invoke_count > 1 && recurse == 0) {
				ret = 0;
				print_entry_attr2(root_filp->name, &statbuf);
				goto err;
			}

			lseek(dir_fd, 0, SEEK_SET);
			do {
				p = (struct dirent64 *) realloc(p, (dirent_input_count * counter++) * sizeof(struct dirent64));
				assert(p);
				ptr = (char *) p + dirent_total_bytes;
				dirent_output_bytes = getdents64(dir_fd, (struct dirent64 *) ptr, dirent_input_count * sizeof(struct dirent64));
				if (dirent_output_bytes <= 0)
					break;
				total_rec_len = 0;
				dirent_total_bytes += dirent_output_bytes;
				while (total_rec_len < dirent_output_bytes) {
					next = (struct dirent64 *) (ptr + total_rec_len);
					total_rec_len += next->d_reclen;
					dirent_total_count++;
				}
			} while (1);
			if (dirent_output_bytes < 0) {
				perror("getdents64");
				goto err;
			}
			ptr = (char *) p;
			total_rec_len = 0;
			for (i = 0; i < dirent_total_count; i++) 
			{
					next = (struct dirent64 *) (ptr + total_rec_len);
					total_rec_len += next->d_reclen;
						if (strcmp(next->d_name, ".") && strcmp(next->d_name, "..")) {
							filp = (struct files *)calloc(1, sizeof(struct files));
							if (!filp) {
								perror("do_root:calloc");
								return -1;
							}
							snprintf(filp->name, NAME_MAX + 1, "%s/%s", root_filp->name, next->d_name);
							/* Add to the tree */
							list_add_tail(&filp->level, &TREE);
						}
			}
			free(p);
		} 
		else if (dir_fd >= 0 && use_direntplus == 1)
		{
			struct dirent64_plus *p = NULL, *next;
			int dirent_input_count = 4096, counter = 1;
			int total_rec_len, i, dirent_total_count = 0, dirent_output_bytes, dirent_total_bytes = 0;
			char *ptr = NULL;

			lseek(dir_fd, 0, SEEK_SET);
			do {
				p = (struct dirent64_plus *) realloc(p, (dirent_input_count * counter++) * sizeof(struct dirent64_plus));
				assert(p);
				ptr = (char *) p + dirent_total_bytes;
				dirent_output_bytes = getdents64_plus(dir_fd, (struct dirent64_plus *) ptr, dirent_input_count * sizeof(struct dirent64_plus));
				if (dirent_output_bytes <= 0)
					break;
				total_rec_len = 0;
				dirent_total_bytes += dirent_output_bytes;
				while (total_rec_len < dirent_output_bytes) {
					next = (struct dirent64_plus *) (ptr + total_rec_len);
					total_rec_len += next->dp_dirent.d_reclen;
					dirent_total_count++;
				}
			} while (1);
			if (dirent_output_bytes < 0) {
				perror("getdents64_plus");
				goto err;
			}
			total_rec_len = 0;
			ptr = (char *) p;
			for (i = 0; i < dirent_total_count; i++) 
			{
					next = (struct dirent64_plus *) (ptr + total_rec_len);
					total_rec_len += next->dp_dirent.d_reclen;
					if (strcmp(next->dp_dirent.d_name, ".") && strcmp(next->dp_dirent.d_name, "..")) 
					{
						print_entry_attr(next->dp_dirent.d_name, &next->dp_stat);
						if (recurse && S_ISDIR(next->dp_stat.st_mode))
						{
							filp = (struct files *)calloc(1, sizeof(struct files));
							if (!filp) {
								perror("do_root:calloc");
								return -1;
							}
							snprintf(filp->name, NAME_MAX + 1, "%s/%s", root_filp->name, next->dp_dirent.d_name);
							/* Add to the tree */
							list_add_tail(&filp->level, &TREE);
						}
					}
			}
			free(p);
		}
	}
	/* or are we looking at a regular file? */
	else {
		if (use_direntplus == 0) {
			print_entry_attr2(root_filp->name, &statbuf);
		}
	} 
err:
	/* Ignore any symbolic links, device-files, any other special files */
	free(root_filp);
	if (dir_fd > 0) close(dir_fd);
	return ret;
}


int do_flatten_hierarchy()
{
	struct files *filp = NULL;

	/* traverse the tree and prune out unnecessary files and flatten the hierarchy */
	path_init(path);
	/* walk the tree */
	while (TREE.next != &TREE) {
		filp = (struct files *)list_entry(TREE.next, struct files, level);
		/* Visit the node */
		if (path_walk(filp) < 0) {
			perror("do_flatten_hierarchy: path_walk:");
			return -1;
		}
	}
	return 0;
}

int main(int argc, char *argv[])
{
	char c;
	struct dirent *de;
	struct dirent64 *de64;
	struct dirent_plus *dirent;
	struct dirent64_plus *dirent64;

	while ((c = getopt(argc, argv, "f:rph")) != EOF)
	{
		switch(c) {
			case 'r':
				recurse = 1;
				break;
			case 'f':
				strcpy(path, optarg);
				break;
			case 'p':
				use_direntplus = 1;
				break;
			case 'h':
			case '?':
			default:
				fprintf(stderr, "Unknown option\n");
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
