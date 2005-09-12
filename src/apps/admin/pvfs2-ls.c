/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <assert.h>
#include <getopt.h>

#include "pvfs2.h"
#include "str-utils.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif


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
    int list_verbose;
    int list_numeric_uid_gid;
    int list_directory;
    int list_no_group;
    int list_almost_all;
    int list_all;
    int list_no_owner;
    int list_inode;
    int list_use_si_units;
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
    char *start,
    int fs_id,
    struct options *opts);

static void print_entry_attr(
    PVFS_handle handle,
    char *entry_name,
    PVFS_sys_attr *attr,
    struct options *opts);

#define print_dot_and_dot_dot_info_if_required(refn)        \
do {                                                        \
    if (opts->list_all && !opts->list_almost_all) {         \
        /*                                                  \
          we have to fake access to the .. handle           \
          since our sysint lookup doesn't return that       \
          kind of intermediate information.  we can get     \
          this value, by manually resolving it with lookups \
          on base dirs, but I'm not sure it's worth it      \
        */                                                  \
        if (opts->list_inode && !opts->list_long) {         \
            printf("%Lu .\n",Lu(refn.handle));              \
            printf("%Lu .. (faked)\n",Lu(refn.handle));     \
        }                                                   \
        else if (opts->list_long) {                         \
            print_entry(".", refn.handle,                   \
                        refn.fs_id, opts);                  \
            print_entry(".. (faked)", refn.handle,          \
                        refn.fs_id, opts);                  \
        }                                                   \
        else {                                              \
            printf(".\n");                                  \
            printf("..\n");                                 \
        }                                                   \
    }                                                       \
} while(0)

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
    time_t mtime = (time_t)attr->mtime;
    struct tm *time = localtime(&mtime);
    PVFS_size size = 0;
    char scratch_owner[16] = {0}, scratch_group[16] = {0};
    char scratch_size[16] = {0}, scratch_inode[16] = {0};
    char f_type = '-';
    char group_x_char = '-';

    if (!opts->list_all && (entry_name[0] == '.'))
    {
        return;
    }

    snprintf(scratch_owner,16,"%d",(int)attr->owner);
    snprintf(scratch_group,16,"%d",(int)attr->group);

    if (opts->list_inode)
    {
        snprintf(scratch_inode,16,"%Lu ",Lu(handle));
        inode = scratch_inode;
    }

    if ((attr->objtype == PVFS_TYPE_METAFILE) &&
        (attr->mask & PVFS_ATTR_SYS_SIZE))
    {
        size = attr->size;
    }
    else if ((attr->objtype == PVFS_TYPE_SYMLINK) &&
             (attr->link_target))
    {
        size = (PVFS_size)strlen(attr->link_target);
    }
    else if (attr->objtype == PVFS_TYPE_DIRECTORY)
    {
        size = (PVFS_size)4096;
    }

    if (opts->list_human_readable)
    {
        PVFS_util_make_size_human_readable(
            size,scratch_size,16,opts->list_use_si_units);
    }
    else
    {
        snprintf(scratch_size,16, "%Ld", Ld(size));
    }
    format_size_string(scratch_size,11,&formatted_size,1,1);

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

    /* for owner and group allow the fields to grow larger than 8 if
     * necessary (set hard_limit to 0), but pad anything smaller to
     * take up 8 spaces.
     */
    format_size_string(owner,8,&formatted_owner,0,0);
    format_size_string(group,8,&formatted_group,0,0);

    if (attr->objtype == PVFS_TYPE_DIRECTORY)
    {
        f_type =  'd';
    }
    else if (attr->objtype == PVFS_TYPE_SYMLINK)
    {
        f_type =  'l';
    }

    /* special case to set setgid display for groups if needed */
    if(attr->perms & PVFS_G_SGID)
    {
        group_x_char = ((attr->perms & PVFS_G_EXECUTE) ? 's' : 'S');
    }
    else
    {
        group_x_char = ((attr->perms & PVFS_G_EXECUTE) ? 'x' : '-');
    }

    snprintf(buf,128,"%s%c%c%c%c%c%c%c%c%c%c    1 %s %s %s "
             "%.4d-%.2d-%.2d %.2d:%.2d %s",
             inode,
             f_type,
             ((attr->perms & PVFS_U_READ) ? 'r' : '-'),
             ((attr->perms & PVFS_U_WRITE) ? 'w' : '-'),
             ((attr->perms & PVFS_U_EXECUTE) ? 'x' : '-'),
             ((attr->perms & PVFS_G_READ) ? 'r' : '-'),
             ((attr->perms & PVFS_G_WRITE) ? 'w' : '-'),
             group_x_char,
             ((attr->perms & PVFS_O_READ) ? 'r' : '-'),
             ((attr->perms & PVFS_O_WRITE) ? 'w' : '-'),
             ((attr->perms & PVFS_O_EXECUTE) ? 'x' : '-'),
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

    if (attr->objtype == PVFS_TYPE_SYMLINK)
    {
        assert(attr->link_target);

        if (opts->list_long)
        {
            printf("%s -> %s\n", buf, attr->link_target);
        }
        else
        {
            printf("%s\n",buf);
        }
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
    int ret = -1;
    PVFS_object_ref ref;
    PVFS_credentials credentials;
    PVFS_sysresp_getattr getattr_response;

    if (!opts->list_long)
    {
        if (opts->list_inode)
        {
            printf("%Lu %s\n", Lu(handle), entry_name);
        }
        else
        {
            printf("%s\n", entry_name);
        }
        return;
    }

    ref.handle = handle;
    ref.fs_id = fs_id;

    memset(&getattr_response,0, sizeof(PVFS_sysresp_getattr));
    PVFS_util_gen_credentials(&credentials);

    ret = PVFS_sys_getattr(ref, PVFS_ATTR_SYS_ALL,
                           &credentials, &getattr_response);
    if (ret)
    {
        fprintf(stderr,"Failed to get attributes on handle %Lu,%d\n",
                Lu(handle),fs_id);
        PVFS_perror("Getattr failure", ret);
        return;
    }
    print_entry_attr(handle, entry_name, &getattr_response.attr, opts);
}

int do_list(
    char *start,
    int fs_id,
    struct options *opts)
{
    int i = 0, printed_dot_info = 0;
    int ret = -1;
    int pvfs_dirent_incount;
    char *name = NULL, *cur_file = NULL;
    PVFS_handle cur_handle;
    PVFS_sysresp_lookup lk_response;
    PVFS_sysresp_readdir rd_response;
    PVFS_sysresp_getattr getattr_response;
    PVFS_credentials credentials;
    PVFS_object_ref ref;
    PVFS_ds_position token;
    uint64_t dir_version = 0;

    name = start;

    memset(&lk_response,0,sizeof(PVFS_sysresp_lookup));
    PVFS_util_gen_credentials(&credentials);

    ret = PVFS_sys_lookup(fs_id, name, &credentials,
                        &lk_response, PVFS2_LOOKUP_LINK_NO_FOLLOW);
    if(ret < 0)
    {
        PVFS_perror("PVFS_sys_lookup", ret);
        return -1;
    }

    ref.handle = lk_response.ref.handle;
    ref.fs_id = fs_id;
    pvfs_dirent_incount = MAX_NUM_DIRENTS;

    memset(&getattr_response,0,sizeof(PVFS_sysresp_getattr));
    if (PVFS_sys_getattr(ref, PVFS_ATTR_SYS_ALL,
                         &credentials, &getattr_response) == 0)
    {
        if ((getattr_response.attr.objtype == PVFS_TYPE_METAFILE) ||
            (getattr_response.attr.objtype == PVFS_TYPE_SYMLINK) ||
            ((getattr_response.attr.objtype == PVFS_TYPE_DIRECTORY) &&
             (opts->list_directory)))
        {
            char segment[128] = {0};
            PVFS_sysresp_getparent getparent_resp;
            PINT_remove_base_dir(name, segment, 128);
            if (strcmp(segment,"") == 0)
            {
                snprintf(segment,128,"/");
            }

            if (getattr_response.attr.objtype == PVFS_TYPE_DIRECTORY)
            {
                if (PVFS_sys_getparent(ref.fs_id, name, &credentials,
                                       &getparent_resp) == 0)
                {
                    print_dot_and_dot_dot_info_if_required(
                        getparent_resp.parent_ref);
                }
            }

            if (opts->list_long)
            {
                print_entry_attr(ref.handle, segment,
                                 &getattr_response.attr, opts);
            }
            else
            {
                print_entry(segment, ref.handle, ref.fs_id, opts);
            }
            return 0;
        }
    }

    token = 0;
    do
    {
        memset(&rd_response, 0, sizeof(PVFS_sysresp_readdir));
        ret = PVFS_sys_readdir(
                ref, (!token ? PVFS_READDIR_START : token),
                pvfs_dirent_incount, &credentials, &rd_response);
        if(ret < 0)
        {
            PVFS_perror("PVFS_sys_readdir", ret);
            return -1;
        }

        if (dir_version == 0)
        {
            dir_version = rd_response.directory_version;
        }
        else if (opts->list_verbose)
        {
            if (dir_version != rd_response.directory_version)
            {
                fprintf(stderr, "*** directory changed! listing may "
                        "not be correct\n");
                dir_version = rd_response.directory_version;
            }
        }

        if (!printed_dot_info)
        {
            /*
              the list_all option prints files starting with .;
              the almost_all option skips the '.', '..' printing
            */
            print_dot_and_dot_dot_info_if_required(ref);
            printed_dot_info = 1;
        }

        for(i = 0; i < rd_response.pvfs_dirent_outcount; i++)
        {
            cur_file = rd_response.dirent_array[i].d_name;
            cur_handle = rd_response.dirent_array[i].handle;

            print_entry(cur_file, cur_handle, fs_id, opts);
        }
        token += rd_response.pvfs_dirent_outcount;

        if (rd_response.pvfs_dirent_outcount)
        {
            free(rd_response.dirent_array);
            rd_response.dirent_array = NULL;
        }

    } while(rd_response.pvfs_dirent_outcount == pvfs_dirent_incount);

    if (rd_response.pvfs_dirent_outcount)
    {
        free(rd_response.dirent_array);
        rd_response.dirent_array = NULL;
    }
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
        {"human-readable",0,0,0},
        {"si",0,0,0},
        {"version",0,0,0},
        {"verbose",0,0,0},
        {"numeric-uid-gid",0,0,0},
        {"directory",0,0,0},
        {"no-group",0,0,0},
        {"almost-all",0,0,0},
        {"all",0,0,0},
        {"inode",0,0,0},
        {"size",0,0,0},
        {0,0,0,0}
    };

    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if (!tmp_opts)
    {
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    while((ret = getopt_long(argc, argv, "hVndGoAaigl",
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
                else if (strcmp("si", cur_option) == 0)
                {
                    tmp_opts->list_use_si_units = 1;
                    break;
                }
                else if (strcmp("version", cur_option) == 0)
                {
                    printf("%s\n", PVFS2_VERSION);
                    exit(0);
                }
                else if (strcmp("verbose", cur_option) == 0)
                {
                    goto list_verbose;
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
            case 'V':
          list_verbose:
                tmp_opts->list_verbose = 1;
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
    fprintf(stderr,"  -h, --human-readable       print sizes in human "
            "readable format\n\t\t\t\t(e.g. 1K 234M 2G)\n");
    fprintf(stderr,"      --si                   likewise, but use powers "
            "of 1000, not 1024\n");
    fprintf(stderr,"  -i, --inode                print index number "
            "of each file\n");
    fprintf(stderr,"  -l                         use a long listing "
            "format\n");
    fprintf(stderr,"  -n, --numeric-uid-gid      like -l, but list "
            "numeric UIDs and GIDs\n");
    fprintf(stderr,"  -o                         like -l, but do not "
            "list group information\n");
    fprintf(stderr,"      --help                 display this help "
            "and exit\n");
    fprintf(stderr,"  -V, --verbose              reports if the dir is "
            "changing during listing\n");
    fprintf(stderr,"      --version              output version "
            "information and exit\n");
    return;
}

int main(int argc, char **argv)
{
    int ret = -1, i = 0;
    char pvfs_path[MAX_NUM_PATHS][PVFS_NAME_MAX];
    PVFS_fs_id fs_id_array[MAX_NUM_PATHS] = {0};
    const PVFS_util_tab* tab;
    struct options* user_opts = NULL;
    char current_dir[PVFS_NAME_MAX] = {0};
    int found_one = 0;

    process_name = argv[0];

    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line "
                "arguments.\n");
 	usage(argc, argv);
	return(-1);
    }

    tab = PVFS_util_parse_pvfstab(NULL);
    if (!tab)
    {
        fprintf(stderr, "Error: failed to parse pvfstab.\n");
        return(-1);
    }

    for(i = 0; i < MAX_NUM_PATHS; i++)
    {
        memset(pvfs_path[i],0,PVFS_NAME_MAX);
    }

    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if (ret < 0)
    {
	PVFS_perror("PVFS_sys_initialize", ret);
	return(-1);
    }

    /* initialize each file system that we found in the tab file */
    for(i = 0; i < tab->mntent_count; i++)
    {
	ret = PVFS_sys_fs_add(&tab->mntent_array[i]);
	if (ret == 0)
        {
	    found_one = 1;
        }
    }

    if (!found_one)
    {
	fprintf(stderr, "Error: could not initialize any file systems "
                "from %s\n", tab->tabfile_name);
	PVFS_sys_finalize();
	return(-1);
    }

    if (user_opts->num_starts == 0)
    {
	snprintf(current_dir,PVFS_NAME_MAX,"%s/",
		 tab->mntent_array[0].mnt_dir);
	user_opts->start[0] = current_dir;
	user_opts->num_starts = 1;
    }

    for(i = 0; i < user_opts->num_starts; i++)
    {
	ret = PVFS_util_resolve(user_opts->start[i],
	    &fs_id_array[i], pvfs_path[i], PVFS_NAME_MAX);
	if ((ret == 0) && (pvfs_path[i][0] == '\0'))
	{
            strcpy(pvfs_path[i], "/");
	}

	if (ret < 0)
	{
	    fprintf(stderr, "Error: could not find file system "
                    "for %s in pvfstab\n", user_opts->start[i]);
	    return(-1);
	}
    }

    for(i = 0; i < user_opts->num_starts; i++)
    {
        if (user_opts->num_starts > 1)
        {
            printf("%s:\n", pvfs_path[i]);
        }

        do_list(pvfs_path[i], fs_id_array[i], user_opts);

        if (user_opts->num_starts > 1)
        {
            printf("\n");
        }
    }

    PVFS_sys_finalize();

    return(ret);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
