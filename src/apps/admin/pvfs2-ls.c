/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <assert.h>
#include <getopt.h>

#include "pvfs2.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

#define DEFAULT_TAB "/etc/pvfs2tab"

#define KILOBYTE                1024
#define MEGABYTE   (1024 * KILOBYTE)
#define GIGABYTE   (1024 * MEGABYTE)
/*
#define TERABYTE   (1024 * GIGABYTE)
#define PETABYTE   (1024 * TERABYTE)
#define  EXABYTE   (1024 * PETABYTE)
#define ZETTABYTE  (1024 *  EXABYTE)
#define YOTTABYTE (1024 * ZETTABYTE)
*/
#define NUM_SIZES                  3

static PVFS_size s_size_table[NUM_SIZES] =
    { /*YOTTABYTE, ZETTABYTE, EXABYTE, PETABYTE,
        TERABYTE,*/ GIGABYTE, MEGABYTE, KILOBYTE };
static char *s_str_size_table[NUM_SIZES] =
    { /*"Y", "Z", "E", "P","T",*/ "G", "M", "K" };

/* TODO: this can be larger after system interface readdir logic
 * is in place to break up large readdirs into multiple operations
 */
#define MAX_NUM_DIRENTS    32

/*
  arbitrarily restrict the number of paths
  that this ls version can take as arguments
*/
#define MAX_NUM_PATHS       8

/* optional parameters, filled in by parse_args() */
struct options
{
    int list_human_readable;
    int list_long;
    int list_numeric_uid_gid;
    int list_directory;
    int list_no_group;
    int list_almost_all;
    int list_all;
    int list_no_owner;
    int list_inode;
    char *start[MAX_NUM_PATHS];
    int num_starts;
};

static char *process_name = NULL;

static struct options* parse_args(int argc, char* argv[]);

static void usage(int argc, char** argv);

static void print_entry(
    char *entry_name,
    PVFS_handle handle,
    PVFS_fs_id fs_id,
    struct options *opts);

static int do_list(
    PVFS_sysresp_init *init_response,
    char *start, struct options *opts);

static void print_entry_attr(
    PVFS_handle handle,
    char *entry_name,
    PVFS_sys_attr *attr,
    struct options *opts);


int main(int argc, char **argv)
{
    int ret = -1, i = 0, j = 0;
    char pvfs_path[MAX_NUM_PATHS][PVFS_NAME_MAX];
    pvfs_mntlist mnt = {0,NULL};
    PVFS_sysresp_init resp_init;
    struct options* user_opts = NULL;
    int mnt_index = -1;
    char current_dir[PVFS_NAME_MAX] = {0};

    process_name = argv[0];

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	usage(argc, argv);
	return(-1);
    }

    /* look at pvfstab */
    if (PVFS_util_parse_pvfstab(DEFAULT_TAB, &mnt))
    {
        fprintf(stderr, "Error: failed to parse pvfstab %s.\n", DEFAULT_TAB);
        return(-1);
    }

    /* see if the destination resides on any of the file systems
     * listed in the pvfstab; find the pvfs fs relative path
     */
    for(i = 0; i < mnt.nr_entry; i++)
    {
        if (user_opts->num_starts == 0)
        {
            snprintf(current_dir,PVFS_NAME_MAX,"%s/",
                     mnt.ptab_p[i].local_mnt_dir);
            user_opts->start[0] = current_dir;
            user_opts->num_starts = 1;
        }

        for(j = 0; j < user_opts->num_starts; j++)
        {
            memset(pvfs_path[j],0,PVFS_NAME_MAX);
            ret = PVFS_util_remove_dir_prefix(
                user_opts->start[j], mnt.ptab_p[i].local_mnt_dir,
                pvfs_path[j], PVFS_NAME_MAX);
            if (ret == 0)
            {
                if ((mnt_index != -1) && (i != mnt_index))
                {
                    ret = -1;
                    PVFS_perror("Cannot use pvfs2-ls across "
                                "mount points at this time!\n",ret);
                    goto main_out;
                }
                else
                {
                    mnt_index = i;
                }
            }
        }
    }

    if (mnt_index == -1)
    {
        /* try to determine the reason for this failure */
        for(j = 0; j < user_opts->num_starts; j++)
        {
            if ((pvfs_path[j] == NULL) || (strlen(pvfs_path[j]) == 0))
            {
                break;
            }
        }

        /* check for the case where a bogus path was requested */
        if (j != user_opts->num_starts)
        {
            fprintf(stderr, "%s: %s: No such file or directory\n",
                    process_name, user_opts->start[j]);
        }
        else
        {
            fprintf(stderr, "Error: could not find filesystem for %s in "
                    "pvfstab %s\n", user_opts->start[j], DEFAULT_TAB);
        }
	return(-1);

    }

    memset(&resp_init, 0, sizeof(resp_init));
    ret = PVFS_sys_initialize(mnt, 0, &resp_init);
    if (ret < 0)
    {
	PVFS_perror("PVFS_sys_initialize", ret);
	return(-1);
    }

    for(i = 0; i < user_opts->num_starts; i++)
    {
        if (user_opts->num_starts > 1)
        {
            printf("%s:\n", pvfs_path[i]);
        }

        do_list(&resp_init, pvfs_path[i], user_opts);

        if (user_opts->num_starts > 1)
        {
            printf("\n");
        }
    }

main_out:

    PVFS_sys_finalize();

    return(ret);
}

static inline void make_size_human_readable(
    PVFS_size size,
    char *out_str,
    int max_out_len)
{
    int i = 0;
    PVFS_size tmp = 0;

    if (out_str)
    {
        for(i = 0; i < NUM_SIZES; i++)
        {
            tmp = size;
            if ((PVFS_size)(tmp / s_size_table[i]) > 0)
            {
                tmp = (PVFS_size)(tmp / s_size_table[i]);
                break;
            }
        }
        if (i == NUM_SIZES)
        {
            snprintf(out_str,16, "%Ld", size);
        }
        else
        {
            snprintf(out_str,max_out_len,"%Ld%s",
                     tmp,s_str_size_table[i]);
        }
    }
}

/*
  build a string of a specified length that's either
  left or right justified based on the src string;
  caller must free ptr passed out as *out_str_p
*/
static inline void format_size_string(char *src_str,
                                      int num_spaces_total,
                                      char **out_str_p,
                                      int right_justified)
{
    int len = 0;
    int spaces_size_allowed = 0;
    char *buf = NULL, *start = NULL, *src_start = NULL;

    assert(src_str);
    len = strlen(src_str);

    spaces_size_allowed = (num_spaces_total ? num_spaces_total : len);
    buf = (char *)malloc(spaces_size_allowed);
    assert(buf);

    memset(buf,0,spaces_size_allowed);

    if ((len > 0) && (len < spaces_size_allowed))
    {
        memset(buf,' ',(spaces_size_allowed - 1));

        src_start = src_str;

        if (right_justified)
        {
            start = &buf[(spaces_size_allowed-(len+1))];
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
    else if (len == 0)
    {
        *out_str_p = strdup(buf);
    }
    free(buf);
}

void print_entry_attr(
    PVFS_handle handle,
    char *entry_name,
    PVFS_sys_attr *attr,
    struct options *opts)
{
    char buf[128] = {0}, *formatted_size = NULL;
    char *formatted_owner = NULL, *formatted_group = NULL;
    struct group *grp = NULL;
    struct passwd *pwd = NULL;
    char *empty_str = "";
    char *owner = empty_str, *group = empty_str;
    char *inode = empty_str;
    struct tm *time = gmtime((time_t *)&attr->atime);
    PVFS_size size = 0;
    char scratch_owner[16] = {0}, scratch_group[16] = {0};
    char scratch_size[16] = {0}, scratch_inode[16] = {0};

    if (!opts->list_all && (entry_name[0] == '.'))
    {
        return;
    }

    snprintf(scratch_owner,16,"%d",(int)attr->owner);
    snprintf(scratch_group,16,"%d",(int)attr->group);

    if (opts->list_inode)
    {
        snprintf(scratch_inode,16,"%Lu ",handle);
        inode = scratch_inode;
    }

    size = (((attr->objtype == PVFS_TYPE_METAFILE) &&
             (attr->mask & PVFS_ATTR_SYS_SIZE)) ?
            (unsigned long)attr->size : 0);

    if (opts->list_human_readable)
    {
        make_size_human_readable(size,scratch_size,16);
    }
    else
    {
        snprintf(scratch_size,16, "%Lu", size);
    }
    format_size_string(scratch_size,11,&formatted_size,1);

    if (!opts->list_no_owner)
    {
        owner = scratch_owner;
    }
    if (!opts->list_no_group)
    {
        group = scratch_group;
    }
    if (!opts->list_numeric_uid_gid)
    {
        if (!opts->list_no_owner)
        {
            pwd = getpwuid((uid_t)attr->owner);
            owner = (pwd ? pwd->pw_name : scratch_owner);
        }

        if (!opts->list_no_group)
        {
            grp = getgrgid((gid_t)attr->group);
            group = (grp ? grp->gr_name : scratch_group);
        }
    }

    format_size_string(owner,8,&formatted_owner,0);
    format_size_string(group,8,&formatted_group,0);

    snprintf(buf,128,"%s%c%c%c%c%c%c%c%c%c%c    1 %s %s %s "
             "%.4d-%.2d-%.2d %.2d:%.2d %s",
             inode,
             ((attr->objtype == PVFS_TYPE_DIRECTORY) ? 'd' : '-'),
             ((attr->perms & PVFS_U_READ) ? 'r' : '-'),
             ((attr->perms & PVFS_U_WRITE) ? 'w' : '-'),
             ((attr->perms & PVFS_U_EXECUTE) ? 'x' : '-'),
             ((attr->perms & PVFS_G_READ) ? 'r' : '-'),
             ((attr->perms & PVFS_G_WRITE) ? 'w' : '-'),
             ((attr->perms & PVFS_G_EXECUTE) ? 'x' : '-'),
             ((attr->perms & PVFS_O_READ) ? 'r' : '-'),
             ((attr->perms & PVFS_O_WRITE) ? 'w' : '-'),
             ((attr->perms & PVFS_O_EXECUTE) ? 'x' : '-'),
             formatted_owner,
             formatted_group,
             formatted_size,
             (time->tm_year + 1900),
             (time->tm_mon + 1),
             time->tm_mday,
             (time->tm_hour + 1),
             (time->tm_min + 1),
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

    if (attr->objtype == PVFS_TYPE_SYMLINK)
    {
        assert(attr->link_target);

        printf("%s -> %s\n", buf, attr->link_target);
        free(attr->link_target);
    }
    else
    {
        printf("%s\n",buf);
    }
}

void print_entry(
    char *entry_name,
    PVFS_handle handle,
    PVFS_fs_id fs_id,
    struct options *opts)
{
    PVFS_pinode_reference pinode_refn;
    PVFS_credentials credentials;
    PVFS_sysresp_getattr getattr_response;

    if (!opts->list_long)
    {
        if (opts->list_inode)
        {
            printf("%Lu %s\n", handle, entry_name);
        }
        else
        {
            printf("%s\n", entry_name);
        }
        return;
    }

    memset(&getattr_response,0, sizeof(PVFS_sysresp_getattr));
    memset(&credentials,0, sizeof(PVFS_credentials));

    credentials.uid = 0;
    credentials.gid = 0;
    
    pinode_refn.handle = handle;
    pinode_refn.fs_id = fs_id;

    if (PVFS_sys_getattr(pinode_refn, PVFS_ATTR_SYS_ALL,
                         credentials, &getattr_response))
    {
        fprintf(stderr,"Failed to get attributes on handle 0x%08Lx "
                "(fs_id is %d)\n",handle,fs_id);
        return;
    }
    print_entry_attr(handle, entry_name, &getattr_response.attr, opts);
}

int do_list(
    PVFS_sysresp_init *init_response,
    char *start,
    struct options *opts)
{
    int i = 0, printed_dot_info = 0;
    int pvfs_dirent_incount;
    char *name = NULL, *cur_file = NULL;
    PVFS_handle cur_handle;
    PVFS_sysresp_lookup lk_response;
    PVFS_sysresp_readdir rd_response;
    PVFS_sysresp_getattr getattr_response;
    PVFS_fs_id fs_id;
    PVFS_credentials credentials;
    PVFS_pinode_reference pinode_refn;
    PVFS_ds_position token;


    name = start;
    fs_id = init_response->fsid_list[0];
    credentials.uid = 0;
    credentials.gid = 0;

    memset(&lk_response,0,sizeof(PVFS_sysresp_lookup));
    if (PVFS_sys_lookup(fs_id, name, credentials, &lk_response))
    {
        fprintf(stderr, "%s: %s: No such file or directory\n",
                process_name, name);
        return -1;
    }

    pinode_refn.handle = lk_response.pinode_refn.handle;
    pinode_refn.fs_id = init_response->fsid_list[0];
    pvfs_dirent_incount = MAX_NUM_DIRENTS;
    credentials.uid = 0;
    credentials.gid = 0;

    memset(&getattr_response,0,sizeof(PVFS_sysresp_getattr));

    if (PVFS_sys_getattr(pinode_refn, PVFS_ATTR_SYS_ALL,
                         credentials, &getattr_response) == 0)
    {
        if ((getattr_response.attr.objtype == PVFS_TYPE_METAFILE) ||
            ((getattr_response.attr.objtype == PVFS_TYPE_DIRECTORY) &&
             (opts->list_directory)))
        {
            char segment[128] = {0};
            PVFS_util_remove_base_dir(name, segment, 128);
            if (strcmp(segment,"") == 0)
            {
                snprintf(segment,128,"/");
            }
            print_entry_attr(pinode_refn.handle, segment,
                             &getattr_response.attr, opts);
            return 0;
        }
    }

    token = 0;
    do
    {
        memset(&rd_response,0,sizeof(PVFS_sysresp_readdir));
        if (PVFS_sys_readdir(pinode_refn,
                             (!token ? PVFS2_READDIR_START : token),
                             pvfs_dirent_incount, credentials, &rd_response))
        {
            fprintf(stderr,"readdir failed\n");
            return -1;
        }

        if (!printed_dot_info)
        {
            /*
              the list_all option prints files starting with .;
              the almost_all option skips the '.', '..' printing
            */
            if (opts->list_all && !opts->list_almost_all)
            {
                /*
                  we have to fake access to the .. handle
                  since our sysint lookup doesn't return that
                  kind of intermediate information.  we can get
                  this value, by manually resolving it with lookups
                  on base dirs, but I'm not sure it's worth it
                */
                if (opts->list_inode && !opts->list_long)
                {
                    printf("%Lu .\n",pinode_refn.handle);
                    printf("%Lu .. (faked)\n",pinode_refn.handle);
                }
                else if (opts->list_long)
                {
                    print_entry(".", pinode_refn.handle,
                                pinode_refn.fs_id, opts);
                    print_entry(".. (faked)", pinode_refn.handle,
                                pinode_refn.fs_id, opts);
                }
                else
                {
                    printf(".\n");
                    printf("..\n");
                }
            }
            printed_dot_info = 1;
        }

        for(i = 0; i < rd_response.pvfs_dirent_outcount; i++)
        {
            cur_file = rd_response.dirent_array[i].d_name;
            cur_handle = rd_response.dirent_array[i].handle;

            print_entry(cur_file, cur_handle,
                        init_response->fsid_list[0], opts);
        }
        token += rd_response.pvfs_dirent_outcount;

        if (rd_response.pvfs_dirent_outcount)
            free(rd_response.dirent_array);

    } while(rd_response.pvfs_dirent_outcount != 0);

    return 0;
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char* argv[])
{
    int i = 0, ret = 0, option_index = 0;
    char *cur_option = NULL;
    struct options* tmp_opts = NULL;
    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"human_readable",0,0,0},
        {"version",0,0,0},
        {"numeric-uid-gid",0,0,0},
        {"directory",0,0,0},
        {"no-group",0,0,0},
        {"almost-all",0,0,0},
        {"all",0,0,0},
        {"inode",0,0,0},
        {"size",0,0,0},
        {0,0,0,0}
    };

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if (!tmp_opts)
    {
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* look at command line arguments */
    while((ret = getopt_long(argc, argv, "hvndGoAaigl",
                             long_opts, &option_index)) != -1)
    {
	switch(ret)
        {
            case 0:
                cur_option = (char *)long_opts[option_index].name;

                if (strcmp("help", cur_option) == 0)
                {
                    usage(argc, argv);
                    exit(0);
                }
                else if (strcmp("human-readable", cur_option) == 0)
                {
                    goto list_human_readable;
                }
                else if (strcmp("version", cur_option) == 0)
                {
                    printf("%s\n", PVFS2_VERSION);
                    exit(0);
                }
                else if (strcmp("numeric-uid-gid", cur_option) == 0)
                {
                    goto list_numeric_uid_gid;
                }
                else if (strcmp("directory", cur_option) == 0)
                {
                    goto list_directory;
                }
                else if (strcmp("no-group", cur_option) == 0)
                {
                    goto list_no_group;
                }
                else if (strcmp("almost-all", cur_option) == 0)
                {
                    goto list_almost_all;
                }
                else if (strcmp("all", cur_option) == 0)
                {
                    goto list_all;
                }
                else if (strcmp("inode", cur_option) == 0)
                {
                    goto list_inode;
                }
                else
                {
                    usage(argc, argv);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'h':
          list_human_readable:
                tmp_opts->list_human_readable = 1;
                break;
	    case 'l':
                tmp_opts->list_long = 1;
		break;
	    case 'n':
          list_numeric_uid_gid:
                tmp_opts->list_long = 1;
                tmp_opts->list_numeric_uid_gid = 1;
		break;
            case 'd':
          list_directory:
                tmp_opts->list_directory = 1;
                break;
	    case 'o':
          list_no_group:
                tmp_opts->list_long = 1;
                tmp_opts->list_no_group = 1;
		break;
            case 'A':
          list_almost_all:
                tmp_opts->list_almost_all = 1;
                break;
	    case 'a':
          list_all:
                tmp_opts->list_all = 1;
		break;
	    case 'g':
                tmp_opts->list_long = 1;
                tmp_opts->list_no_owner = 1;
		break;
            case 'i':
          list_inode:
                tmp_opts->list_inode = 1;
                break;
	    case '?':
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    for(i = optind; i < argc; i++)
    {
        if (tmp_opts->num_starts < MAX_NUM_PATHS)
        {
            tmp_opts->start[i-optind] = argv[i];
            tmp_opts->num_starts++;
        }
        else
        {
            fprintf(stderr,"Ignoring path %s\n",argv[i]);
        }
    }
    return tmp_opts;
}

static void usage(int argc, char** argv)
{
    fprintf(stderr,  "Usage: %s [OPTION]... [FILE]...\n", argv[0]); 
    fprintf(stderr, "List information about the FILEs (the current "
            "directory by default)\n\n");
    fprintf(stderr,"  -a, --all                  "
            "do not hide entries starting with .\n");
    fprintf(stderr,"  -A, --almost-all           do not list "
            "implied . and ..\n");
    fprintf(stderr,"  -d, --directory            list directory "
            "entries instead of contents\n");
    fprintf(stderr,"  -g                         like -l, but do "
            "not list owner\n");
    fprintf(stderr,"  -G, --no-group             inhibit display "
            "of group information\n");
    fprintf(stderr,"  -h, --human-readable  print sizes in human "
            "readable format (e.g., 1K 234M 2G)\n");
    fprintf(stderr,"  -i, --inode                print index number "
            "of each file\n");
    fprintf(stderr,"  -l                         use a long listing "
            "format\n");
    fprintf(stderr,"  -n, --numeric-uid-gid      like -l, but list "
            "numeric UIDs and GIDs\n");
    fprintf(stderr,"  -o                         like -l, but do not "
            "list group information\n");
    fprintf(stderr,"      --help     display this help and exit\n");
    fprintf(stderr,"      --version  output version information "
            "and exit\n");
    fprintf(stderr, "\n      Note: this utility reads /etc/pvfs2tab "
            "for file system configuration.\n");
    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
