/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>

#include "pvfs2-config.h"
#include "pvfs2-sysint.h"
#include "pvfs2-util.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "str-utils.h"
#include "gen-locks.h"

/* TODO: add replacement functions for systems without getmntent() */
#ifndef HAVE_GETMNTENT
#error HAVE_GETMNTENT undefined! needed for parse_pvfstab
#endif

#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif

#define PVFS2_MAX_TABFILES       8
#define PVFS2_DYNAMIC_TAB_INDEX  (PVFS2_MAX_TABFILES - 1)
#define PVFS2_DYNAMIC_TAB_NAME   "<DynamicTab>"

static PVFS_util_tab s_stat_tab_array[PVFS2_MAX_TABFILES];
static int s_stat_tab_count = 0;
static gen_mutex_t s_stat_tab_mutex = GEN_MUTEX_INITIALIZER;

static int parse_flowproto_string(
    const char *input,
    enum PVFS_flowproto_type *flowproto);

static int parse_encoding_string(
    const char *cp,
    enum PVFS_encoding_type *et);

static int copy_mntent(
    struct PVFS_sys_mntent *dest_mntent,
    struct PVFS_sys_mntent *src_mntent);

void PVFS_util_gen_credentials(
    PVFS_credentials *credentials)
{
    assert(credentials);

    memset(credentials, 0, sizeof(PVFS_credentials));
    credentials->uid = getuid();
    credentials->gid = getgid();
}

/* PVFS_util_parse_pvfstab()
 *
 * parses either the file pointed to by the PVFS2TAB_FILE env
 * variable, or /etc/fstab, or /etc/pvfs2tab or ./pvfs2tab to extract
 * pvfs2 mount entries.
 * 
 * NOTE: if tabfile argument is given at runtime to specify which
 * tabfile to use, then that will be the _only_ file searched for
 * pvfs2 entries.
 *
 * example entry:
 * tcp://localhost:3334/pvfs2-fs /mnt/pvfs2 pvfs2 defaults 0 0
 *
 * returns const pointer to internal tab structure on success, NULL on
 * failure
 */
const PVFS_util_tab *PVFS_util_parse_pvfstab(
    const char *tabfile)
{
    FILE *mnt_fp = NULL;
    int file_count = 4;
    const char *file_list[4] =
        { NULL, "/etc/fstab", "/etc/pvfs2tab", "pvfs2tab" };
    const char *targetfile = NULL;
    struct mntent *tmp_ent;
    int i, j;
    int slashcount = 0;
    char *slash = NULL;
    char *last_slash = NULL;
    const char *cp;
    int ret = -1;
    int tmp_mntent_count = 0;
    PVFS_util_tab *current_tab = NULL;

    if (tabfile != NULL)
    {
        /*
          caller wants us to look in a specific location for the
          tabfile
        */
        file_list[0] = tabfile;
        file_count = 1;
    }
    else
    {
        /*
          search the system and env vars for tab files;
          first check for environment variable override
        */
        file_list[0] = getenv("PVFS2TAB_FILE");
        file_count = 4;
    }

    gen_mutex_lock(&s_stat_tab_mutex);

    /* start by checking list of files we have already parsed */
    for (i = 0; i < s_stat_tab_count; i++)
    {
        for (j = 0; j < file_count; j++)
        {
            if (file_list[j] &&
                !strcmp(file_list[j], s_stat_tab_array[i].tabfile_name))
            {
                /* already done */
                gen_mutex_unlock(&s_stat_tab_mutex);
                return (&s_stat_tab_array[i]);
            }
        }
    }

    assert(s_stat_tab_count < PVFS2_DYNAMIC_TAB_INDEX);

    /* scan our prioritized list of tab files in order, stop when we
     * find one that has at least one pvfs2 entry
     */
    for (i = 0; (i < file_count && !targetfile); i++)
    {
        mnt_fp = setmntent(file_list[i], "r");
        if (mnt_fp)
        {
            while ((tmp_ent = getmntent(mnt_fp)))
            {
                if (strcmp(tmp_ent->mnt_type, "pvfs2") == 0)
                {
                    targetfile = file_list[i];
                    tmp_mntent_count++;
                }
            }
            endmntent(mnt_fp);
        }
    }

    if (!targetfile)
    {
        gossip_err("Error: could not find any pvfs2 tabfile entries.\n");
        gossip_err("Error: tried the following tabfiles:\n");
        for (i = 0; i < file_count; i++)
        {
            gossip_err("       %s\n", file_list[i]);
        }
        gen_mutex_unlock(&s_stat_tab_mutex);
        return (NULL);
    }
    gossip_debug(GOSSIP_CLIENT_DEBUG,
                 "Using pvfs2 tab file: %s\n", targetfile);

    /* allocate array of entries */
    current_tab = &s_stat_tab_array[s_stat_tab_count];
    current_tab->mntent_array = (struct PVFS_sys_mntent *)malloc(
        (tmp_mntent_count * sizeof(struct PVFS_sys_mntent)));
    if (!current_tab->mntent_array)
    {
        gen_mutex_unlock(&s_stat_tab_mutex);
        return (NULL);
    }
    memset(current_tab->mntent_array, 0,
           (tmp_mntent_count * sizeof(struct PVFS_sys_mntent)));
    for (i = 0; i < tmp_mntent_count; i++)
    {
        current_tab->mntent_array[i].fs_id = PVFS_FS_ID_NULL;
    }
    current_tab->mntent_count = tmp_mntent_count;

    /* reopen our chosen fstab file */
    mnt_fp = setmntent(targetfile, "r");
    assert(mnt_fp);

    /* scan through looking for every pvfs2 entry */
    i = 0;
    while ((tmp_ent = getmntent(mnt_fp)))
    {
        if (strcmp(tmp_ent->mnt_type, "pvfs2") == 0)
        {
            slash = tmp_ent->mnt_fsname;
            slashcount = 0;
            while ((slash = index(slash, '/')))
            {
                slash++;
                slashcount++;
            }

            /* find a reference point in the string */
            last_slash = rindex(tmp_ent->mnt_fsname, '/');

            if (slashcount != 3)
            {
                gossip_lerr("Error: invalid tab file entry: %s\n",
                            tmp_ent->mnt_fsname);
                endmntent(mnt_fp);
                gen_mutex_unlock(&s_stat_tab_mutex);
                return (NULL);
            }

            /* allocate room for our copies of the strings */
            current_tab->mntent_array[i].pvfs_config_server =
                (char *)malloc(strlen(tmp_ent->mnt_fsname) + 1);
            current_tab->mntent_array[i].mnt_dir =
                (char *)malloc(strlen(tmp_ent->mnt_dir) + 1);
            current_tab->mntent_array[i].mnt_opts =
                (char *)malloc(strlen(tmp_ent->mnt_opts) + 1);

            /* bail if any mallocs failed */
            if (!current_tab->mntent_array[i].pvfs_config_server ||
                !current_tab->mntent_array[i].mnt_dir ||
                !current_tab->mntent_array[i].mnt_opts)
            {
                goto error_exit;
            }

            /* make our own copy of parameters of interest */
            /* config server and fs name are a special case, take one 
             * string and split it in half on "/" delimiter
             */
            *last_slash = '\0';
            strcpy(current_tab->mntent_array[i].pvfs_config_server,
                   tmp_ent->mnt_fsname);
            last_slash++;
            current_tab->mntent_array[i].pvfs_fs_name =
                strdup(last_slash);
            if (!current_tab->mntent_array[i].pvfs_fs_name)
            {
                goto error_exit;
            }

            /* mnt_dir and mnt_opts are verbatim copies */
            strcpy(current_tab->mntent_array[i].mnt_dir,
                   tmp_ent->mnt_dir);
            strcpy(current_tab->mntent_array[i].mnt_opts,
                   tmp_ent->mnt_opts);

            /* find out if a particular flow protocol was specified */
            if ((hasmntopt(tmp_ent, "flowproto")))
            {
                ret = parse_flowproto_string(
                    tmp_ent->mnt_opts,
                    &(current_tab->
                      mntent_array[i].flowproto));
                if (ret < 0)
                {
                    goto error_exit;
                }
            }
            else
            {
                current_tab->mntent_array[i].flowproto =
                    FLOWPROTO_DEFAULT;
            }

            /* pick an encoding to use with the server */
            current_tab->mntent_array[i].encoding =
                ENCODING_DEFAULT;
            cp = hasmntopt(tmp_ent, "encoding");
            if (cp)
            {
                ret = parse_encoding_string(
                    cp, &current_tab->mntent_array[i].encoding);
                if (ret < 0)
                {
                    goto error_exit;
                }
            }
            i++;
        }
    }
    s_stat_tab_count++;
    strcpy(s_stat_tab_array[s_stat_tab_count-1].tabfile_name, targetfile);
    gen_mutex_unlock(&s_stat_tab_mutex);
    return (&s_stat_tab_array[s_stat_tab_count - 1]);

  error_exit:
    for (; i > -1; i--)
    {
        if (current_tab->mntent_array[i].pvfs_config_server)
        {
            free(current_tab->mntent_array[i].
                 pvfs_config_server);
            current_tab->mntent_array[i].pvfs_config_server =
                NULL;
        }

        if (current_tab->mntent_array[i].mnt_dir)
        {
            free(current_tab->mntent_array[i].mnt_dir);
            current_tab->mntent_array[i].mnt_dir = NULL;
        }

        if (current_tab->mntent_array[i].mnt_opts)
        {
            free(current_tab->mntent_array[i].mnt_opts);
            current_tab->mntent_array[i].mnt_opts = NULL;
        }

        if (current_tab->mntent_array[i].pvfs_fs_name)
        {
            free(current_tab->mntent_array[i].pvfs_fs_name);
            current_tab->mntent_array[i].pvfs_fs_name = NULL;
        }
    }
    endmntent(mnt_fp);
    gen_mutex_unlock(&s_stat_tab_mutex);
    return (NULL);
}

/* PVFS_util_get_default_fsid()
 *
 * fills in the fs identifier for the first active file system that
 * the library knows about.  Useful for test programs or admin tools
 * that need default file system to access if the user has not
 * specified one
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_util_get_default_fsid(PVFS_fs_id* out_fs_id)
{
    int i = 0, j = 0;

    gen_mutex_lock(&s_stat_tab_mutex);

    for(i = 0; i < s_stat_tab_count; i++)
    {
        for(j = 0; j < s_stat_tab_array[i].mntent_count; j++)
        {
            *out_fs_id = s_stat_tab_array[i].mntent_array[j].fs_id;
            if(*out_fs_id != PVFS_FS_ID_NULL)
            {
                gen_mutex_unlock(&s_stat_tab_mutex);
                return(0);
            }
        }
    }

    /* check the dynamic tab area if we haven't found an fs yet */
    for(j = 0; j < s_stat_tab_array[
            PVFS2_DYNAMIC_TAB_INDEX].mntent_count; j++)
    {
        *out_fs_id = s_stat_tab_array[
            PVFS2_DYNAMIC_TAB_INDEX].mntent_array[j].fs_id;
        if(*out_fs_id != PVFS_FS_ID_NULL)
        {
            gen_mutex_unlock(&s_stat_tab_mutex);
            return(0);
        }
    }

    gen_mutex_unlock(&s_stat_tab_mutex);
    return(-PVFS_ENOENT);
}

/*
 * PVFS_util_add_dynamic_mntent()
 *
 * dynamically add mount information to our internally managed mount
 * tables (used for quick fs resolution using PVFS_util_resolve).
 * dynamic mnt entries can only be added to a particular dynamic
 * region of our book keeping, so they're the exception, not the rule.
 *
 * returns 0 on success, -PVFS_error on failure, and 1 if the mount
 * entry already exists as a parsed entry (not dynamic)
 */
int PVFS_util_add_dynamic_mntent(struct PVFS_sys_mntent *mntent)
{
    int i = 0, j = 0, new_index = 0;
    int ret = -PVFS_EINVAL;
    struct PVFS_sys_mntent *current_mnt = NULL;
    struct PVFS_sys_mntent *tmp_mnt_array = NULL;

    if (mntent)
    {
        gen_mutex_lock(&s_stat_tab_mutex);

        /*
          we exhaustively scan to be sure this mnt entry doesn't exist
          anywhere in our book keeping; first scan the parsed regions
        */
        for(i = 0; i < s_stat_tab_count; i++)
        {
            for(j = 0; j < s_stat_tab_array[i].mntent_count; j++)
            {
                current_mnt = &(s_stat_tab_array[i].mntent_array[j]);

                if (current_mnt->fs_id == mntent->fs_id)
                {
                    /*
                      no need to add the dynamic mount information
                      because the file system already exists as a
                      parsed mount entry
                    */
                    gen_mutex_unlock(&s_stat_tab_mutex);
                    return 1;
                }
            }
        }

        /* check the dynamic region if we haven't found a match yet */
        for(j = 0; j < s_stat_tab_array[
                PVFS2_DYNAMIC_TAB_INDEX].mntent_count; j++)
        {
            current_mnt = &(s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].
                            mntent_array[j]);

            if (current_mnt->fs_id == mntent->fs_id)
            {
                gossip_debug(
                    GOSSIP_CLIENT_DEBUG, "* File system %d already "
                    "mounted on %s already exists [dynamic]\n",
                    mntent->fs_id, current_mnt->mnt_dir);

                gen_mutex_unlock(&s_stat_tab_mutex);
                return -PVFS_EEXIST;
            }
        }

        /* copy the mntent to our table in the dynamic tab area */
        new_index = s_stat_tab_array[
            PVFS2_DYNAMIC_TAB_INDEX].mntent_count;

        if (new_index == 0)
        {
            /* allocate and initialize the dynamic tab object */
            s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_array =
                (struct PVFS_sys_mntent *)malloc(
                    sizeof(struct PVFS_sys_mntent));
            if (!s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_array)
            {
                return -PVFS_ENOMEM;
            }
            strncpy(s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].tabfile_name,
                    PVFS2_DYNAMIC_TAB_NAME, PVFS_NAME_MAX);
        }
        else
        {
            /* we need to re-alloc this guy to add a new array entry */
            tmp_mnt_array = (struct PVFS_sys_mntent *)malloc(
                ((new_index + 1) * sizeof(struct PVFS_sys_mntent)));
            if (!tmp_mnt_array)
            {
                return -PVFS_ENOMEM;
            }

            /*
              copy all mntent entries into the new array, freeing the
              original entries
            */
            for(i = 0; i < new_index; i++)
            {
                current_mnt = &s_stat_tab_array[
                    PVFS2_DYNAMIC_TAB_INDEX].mntent_array[i];
                copy_mntent(&tmp_mnt_array[i], current_mnt);
                PVFS_sys_free_mntent(current_mnt);
            }

            /* finally, swap the mntent arrays */
            free(s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_array);
            s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_array =
                tmp_mnt_array;
        }

        gossip_debug(GOSSIP_CLIENT_DEBUG, "* Adding new dynamic mount "
                     "point %s [%d,%d]\n", mntent->mnt_dir,
                     PVFS2_DYNAMIC_TAB_INDEX, new_index);

        current_mnt = &s_stat_tab_array[
            PVFS2_DYNAMIC_TAB_INDEX].mntent_array[new_index];

        ret = copy_mntent(current_mnt, mntent);

        s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_count++;

        gen_mutex_unlock(&s_stat_tab_mutex);
    }
    return ret;
}

/*
 * PVFS_util_remove_internal_mntent()
 *
 * dynamically remove mount information from our internally managed
 * mount tables.
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_util_remove_internal_mntent(
    struct PVFS_sys_mntent *mntent)
{
    int i = 0, j = 0, new_count = 0, found = 0, found_index = 0;
    int ret = -PVFS_EINVAL;
    struct PVFS_sys_mntent *current_mnt = NULL;
    struct PVFS_sys_mntent *tmp_mnt_array = NULL;

    if (mntent)
    {
        gen_mutex_lock(&s_stat_tab_mutex);

        /*
          we exhaustively scan to be sure this mnt entry *does* exist
          somewhere in our book keeping
        */
        for(i = 0; i < s_stat_tab_count; i++)
        {
            for(j = 0; j < s_stat_tab_array[i].mntent_count; j++)
            {
                current_mnt = &(s_stat_tab_array[i].mntent_array[j]);
                if (current_mnt->fs_id == mntent->fs_id)
                {
                    found_index = i;
                    found = 1;
                    goto mntent_found;
                }
            }
        }

        /* check the dynamic region if we haven't found a match yet */
        for(j = 0; j < s_stat_tab_array[
                PVFS2_DYNAMIC_TAB_INDEX].mntent_count; j++)
        {
            current_mnt = &(s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].
                            mntent_array[j]);

            if (current_mnt->fs_id == mntent->fs_id)
            {
                found_index = PVFS2_DYNAMIC_TAB_INDEX;
                found = 1;
                goto mntent_found;
            }
        }

      mntent_found:
        if (!found)
        {
            return -PVFS_EINVAL;
        }

        gossip_debug(GOSSIP_CLIENT_DEBUG, "* Removing mount "
                     "point %s [%d,%d]\n", current_mnt->mnt_dir,
                     found_index, j);

        /* remove the mntent from our table in the found tab area */
        if ((s_stat_tab_array[found_index].mntent_count - 1) > 0)
        {
            /*
              this is 1 minus the old count since there will be 1 less
              mnt entries after this call
            */
            new_count = s_stat_tab_array[found_index].mntent_count - 1;

            /* we need to re-alloc this guy to remove the array entry */
            tmp_mnt_array = (struct PVFS_sys_mntent *)malloc(
                (new_count * sizeof(struct PVFS_sys_mntent)));
            if (!tmp_mnt_array)
            {
                return -PVFS_ENOMEM;
            }

            /*
              copy all mntent entries into the new array, freeing the
              original entries -- and skipping the one that we're
              trying to remove
            */
            for(i = 0, new_count = 0;
                i < s_stat_tab_array[found_index].mntent_count; i++)
            {
                current_mnt = &s_stat_tab_array[found_index].mntent_array[i];

                if (current_mnt->fs_id == mntent->fs_id)
                {
                    PVFS_sys_free_mntent(current_mnt);
                    continue;
                }
                copy_mntent(&tmp_mnt_array[new_count++], current_mnt);
            }

            /* finally, swap the mntent arrays */
            free(s_stat_tab_array[found_index].mntent_array);
            s_stat_tab_array[found_index].mntent_array = tmp_mnt_array;

            s_stat_tab_array[found_index].mntent_count--;
            ret = 0;
        }
        else
        {
            /*
              special case: we're removing the last mnt entry in the
              array here.  since this is the case, we also free the
              array since we know it's now empty.
            */
            PVFS_sys_free_mntent(
                &s_stat_tab_array[found_index].mntent_array[0]);
            free(s_stat_tab_array[found_index].mntent_array);
            s_stat_tab_array[found_index].mntent_array = NULL;
            s_stat_tab_array[found_index].mntent_count = 0;
            ret = 0;
        }
        gen_mutex_unlock(&s_stat_tab_mutex);
    }
    return ret;
}

/* PVFS_util_resolve()
 *
 * given a local path of a file that resides on a pvfs2 volume,
 * determine what the fsid and fs relative path is
 *
 * returns 0 on succees, -PVFS_error on failure
 */
int PVFS_util_resolve(
    const char* local_path,
    PVFS_fs_id* out_fs_id,
    char* out_fs_path,
    int out_fs_path_max)
{
    int i = 0, j = 0;
    int ret = -PVFS_EINVAL;

    gen_mutex_lock(&s_stat_tab_mutex);

    for(i=0; i < s_stat_tab_count; i++)
    {
        for(j=0; j<s_stat_tab_array[i].mntent_count; j++)
        {
            ret = PVFS_util_remove_dir_prefix(
                local_path, s_stat_tab_array[i].mntent_array[j].mnt_dir,
                out_fs_path, out_fs_path_max);
            if(ret == 0)
            {
                *out_fs_id = s_stat_tab_array[i].mntent_array[j].fs_id;
                if(*out_fs_id == PVFS_FS_ID_NULL)
                {
                    gossip_err("Error: %s resides on a PVFS2 file system "
                    "that has not yet been initialized.\n", local_path);

                    gen_mutex_unlock(&s_stat_tab_mutex);
                    return(-PVFS_ENXIO);
                }
                gen_mutex_unlock(&s_stat_tab_mutex);
                return(0);
            }
        }
    }

    /* check the dynamic tab area if we haven't resolved anything yet */
    for(j = 0; j < s_stat_tab_array[
            PVFS2_DYNAMIC_TAB_INDEX].mntent_count; j++)
    {
        ret = PVFS_util_remove_dir_prefix(
            local_path, s_stat_tab_array[
                PVFS2_DYNAMIC_TAB_INDEX].mntent_array[j].mnt_dir,
            out_fs_path, out_fs_path_max);
        if (ret == 0)
        {
            *out_fs_id = s_stat_tab_array[
                PVFS2_DYNAMIC_TAB_INDEX].mntent_array[j].fs_id;
            if(*out_fs_id == PVFS_FS_ID_NULL)
            {
                gossip_err("Error: %s resides on a PVFS2 file system "
                           "that has not yet been initialized.\n",
                           local_path);

                gen_mutex_unlock(&s_stat_tab_mutex);
                return(-PVFS_ENXIO);
            }
            gen_mutex_unlock(&s_stat_tab_mutex);
            return(0);
        }
    }

    gen_mutex_unlock(&s_stat_tab_mutex);
    return(-PVFS_ENOENT);
}

/* PVFS_util_init_defaults()
 *
 * performs the standard set of initialization steps for the system
 * interface, mostly just a wrapper function
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_util_init_defaults(void)
{
    int ret = -1;
    int i;
    int found_one = 0;

    /* use standard system tab files */
    const PVFS_util_tab* tab = PVFS_util_parse_pvfstab(NULL);
    if(!tab)
    {
        gossip_err(
            "Error: failed to find any pvfs2 file systems in the "
            "standard system tab files.\n");
        return(-PVFS_ENOENT);
    }

    /* initialize pvfs system interface */
    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if(ret < 0)
    {
        return(ret);
    }

    /* add in any file systems we found in the fstab */
    for(i=0; i<tab->mntent_count; i++)
    {
        ret = PVFS_sys_fs_add(&tab->mntent_array[i]);
        if(ret == 0)
        {
            found_one = 1;
        }
        else
        {
            gossip_err(
                "WARNING: failed to initialize file system for mount "
                "point %s in tab file %s\n", tab->mntent_array[i].mnt_dir,
                tab->tabfile_name);
        }
    }

    if(found_one)
    {
        return(0);
    }
    else
    {
        gossip_err(
            "ERROR: could not initialize any file systems in %s.\n",
            tab->tabfile_name);
        return(-PVFS_ENODEV);
    }
}

/* PVFS_util_lookup_parent()
 *
 * given a pathname and an fsid, looks up the handle of the parent
 * directory
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_util_lookup_parent(
    char *filename,
    PVFS_fs_id fs_id,
    PVFS_credentials credentials,
    PVFS_handle * handle)
{
    char buf[PVFS_SEGMENT_MAX] = { 0 };
    PVFS_sysresp_lookup resp_look;
    int ret = -1;

    memset(&resp_look, 0, sizeof(PVFS_sysresp_lookup));

    if (PINT_get_base_dir(filename, buf, PVFS_SEGMENT_MAX))
    {
        if (filename[0] != '/')
        {
            gossip_err("Invalid dirname (no leading '/')\n");
        }
        gossip_err("cannot get parent directory of %s\n", filename);
        /* TODO: use defined name for this */
        *handle = 0;
        return (-EINVAL);
    }

    ret = PVFS_sys_lookup(fs_id, buf, credentials,
                          &resp_look, PVFS2_LOOKUP_LINK_FOLLOW);
    if (ret < 0)
    {
        gossip_err("Lookup failed on %s\n", buf);
        /* TODO: use defined name for this */
        *handle = 0;
        return (ret);
    }
    *handle = resp_look.ref.handle;
    return (0);
}


/* PVFS_util_remove_base_dir()
 *
 * Get absolute path minus the base dir
 *
 * Parameters:
 * pathname     - pointer to directory string
 * out_base_dir - pointer to out dir string
 * max_out_len  - max length of out_base_dir buffer
 *
 * All incoming arguments must be valid and non-zero
 *
 * Returns 0 on success; -1 if args are invalid
 *
 * Example inputs and outputs/return values:
 *
 * pathname: /tmp/foo     - out_base_dir: foo       - returns  0
 * pathname: /tmp/foo/bar - out_base_dir: bar       - returns  0
 *
 *
 * invalid pathname input examples:
 * pathname: /            - out_base_dir: undefined - returns -1
 * pathname: NULL         - out_base_dir: undefined - returns -1
 * pathname: foo          - out_base_dir: undefined - returns -1
 *
 */
int PVFS_util_remove_base_dir(
    char *pathname,
    char *out_dir,
    int out_max_len)
{
    int ret = -1, len = 0;
    char *start, *end, *end_ref;

    if (pathname && out_dir && out_max_len)
    {
        if ((strcmp(pathname, "/") == 0) || (pathname[0] != '/'))
        {
            return ret;
        }

        start = pathname;
        end = (char *) (pathname + strlen(pathname));
        end_ref = end;

        while (end && (end > start) && (*(--end) != '/'));

        len = end_ref - ++end;
        if (len < out_max_len)
        {
            memcpy(out_dir, end, len);
            out_dir[len] = '\0';
            ret = 0;
        }
    }
    return ret;
}

/* PVFS_util_remove_dir_prefix()
 *
 * Strips prefix directory out of the path, output includes beginning
 * slash
 *
 * Parameters:
 * pathname     - pointer to directory string (absolute)
 * prefix       - pointer to prefix dir string (absolute)
 * out_path     - pointer to output dir string
 * max_out_len  - max length of out_base_dir buffer
 *
 * All incoming arguments must be valid and non-zero
 *
 * Returns 0 on success; -errno on failure
 *
 * Example inputs and outputs/return values:
 *
 * pathname: /mnt/pvfs2/foo, prefix: /mnt/pvfs2
 *     out_path: /foo, returns 0
 * pathname: /mnt/pvfs2/foo, prefix: /mnt/pvfs2/
 *     out_path: /foo, returns 0
 * pathname: /mnt/pvfs2/foo/bar, prefix: /mnt/pvfs2
 *     out_path: /foo/bar, returns 0
 * pathname: /mnt/pvfs2/foo/bar, prefix: /
 *     out_path: /mnt/pvfs2/foo/bar, returns 0
 *
 * invalid pathname input examples:
 * pathname: /mnt/foo/bar, prefix: /mnt/pvfs2
 *     out_path: undefined, returns -ENOENT
 * pathname: /mnt/pvfs2fake/foo/bar, prefix: /mnt/pvfs2
 *     out_path: undefined, returns -ENOENT
 * pathname: /mnt/foo/bar, prefix: mnt/pvfs2
 *     out_path: undefined, returns -EINVAL
 * pathname: mnt/foo/bar, prefix: /mnt/pvfs2
 *     out_path: undefined, returns -EINVAL
 * out_max_len not large enough for buffer, returns -ENAMETOOLONG
 */
int PVFS_util_remove_dir_prefix(
    const char *pathname,
    const char *prefix,
    char *out_path,
    int out_max_len)
{
    int ret = -EINVAL;
    int prefix_len, pathname_len;
    int cut_index;

    if (!pathname || !prefix || !out_path || !out_max_len)
    {
        return (-EINVAL);
    }

    /* make sure we are given absolute paths */
    if ((pathname[0] != '/') || (prefix[0] != '/'))
    {
        return ret;
    }

    while (pathname[1] == '/')
        pathname++;

    prefix_len = strlen(prefix);
    pathname_len = strlen(pathname);

    /* account for trailing slashes on prefix */
    while (prefix[prefix_len - 1] == '/')
    {
        prefix_len--;
    }

    /* if prefix_len is now zero, then prefix must have been root
     * directory; return copy of entire pathname
     */
    if (prefix_len == 0)
    {
        cut_index = 0;
    }
    else
    {

        /* make sure prefix would fit in pathname */
        if (prefix_len > (pathname_len + 1))
            return (-ENOENT);

        /* see if we can find prefix at beginning of path */
        if (strncmp(prefix, pathname, prefix_len) == 0)
        {
            /* apparent match; see if next element is a slash */
            if ((pathname[prefix_len] != '/') &&
                (pathname[prefix_len] != '\0'))
                return (-ENOENT);

            /* this was indeed a match */
            /* in the case of no trailing slash cut_index will point to the end
             * of "prefix" (NULL).   */
            cut_index = prefix_len;
        }
        else
        {
            return (-ENOENT);
        }
    }

    /* if we hit this point, then we were successful */

    /* is the buffer large enough? */
    if ((1 + strlen(&(pathname[cut_index]))) > out_max_len)
        return (-ENAMETOOLONG);

    /* try to handle the case of no trailing slash */
    if (pathname[cut_index] == '\0')
    {
        out_path[0] = '/';
        out_path[1] = '\0';
    }
    else
        /* copy out appropriate part of pathname */
        strcpy(out_path, &(pathname[cut_index]));

    return (0);
}

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

static PVFS_size PINT_s_size_table[NUM_SIZES] =
{
    /*YOTTABYTE, ZETTABYTE, EXABYTE, PETABYTE, TERABYTE, */
    GIGABYTE, MEGABYTE, KILOBYTE
};

static char *PINT_s_str_size_table[NUM_SIZES] =
{
    /*"Y", "Z", "E", "P","T", */
    "G", "M", "K"
};

/*
 * PVFS_util_make_size_human_readable
 *
 * converts a size value to a human readable string format
 *
 * size         - numeric size of file
 * out_str      - nicely formatted string, like "3.4M"
 *                  (caller must allocate this string)
 * max_out_len  - maximum lenght of out_str
 */
void PVFS_util_make_size_human_readable(
    PVFS_size size,
    char *out_str,
    int max_out_len)
{
    int i = 0;
    PVFS_size tmp = 0;

    if (out_str)
    {
        for (i = 0; i < NUM_SIZES; i++)
        {
            tmp = size;
            if ((PVFS_size) (tmp / PINT_s_size_table[i]) > 0)
            {
                tmp = (PVFS_size) (tmp / PINT_s_size_table[i]);
                break;
            }
        }
        if (i == NUM_SIZES)
        {
            snprintf(out_str, 16, "%Ld", Ld(size));
        }
        else
        {
            snprintf(out_str, max_out_len, "%Ld%s",
                     Ld(tmp), PINT_s_str_size_table[i]);
        }
    }
}

/* parse_flowproto_string()
 *
 * looks in the mount options string for a flowprotocol specifier and 
 * sets the flowproto type accordingly
 *
 * returns 0 on success, -PVFS_error on failure
 */
static int parse_flowproto_string(
    const char *input,
    enum PVFS_flowproto_type *flowproto)
{
    int ret = 0;
    char *start = NULL;
    char flow[256];
    char *comma = NULL;

    start = strstr(input, "flowproto");
    /* we must find a match if this function is being called... */
    assert(start);

    /* scan out the option */
    ret = sscanf(start, "flowproto = %255s ,", flow);
    if (ret != 1)
    {
        gossip_err("Error: malformed flowproto option in tab file.\n");
        return (-PVFS_EINVAL);
    }

    /* chop it off at any trailing comma */
    comma = index(flow, ',');
    if (comma)
    {
        comma[0] = '\0';
    }

    if (!strcmp(flow, "bmi_trove"))
        *flowproto = FLOWPROTO_BMI_TROVE;
    else if (!strcmp(flow, "dump_offsets"))
        *flowproto = FLOWPROTO_DUMP_OFFSETS;
    else if (!strcmp(flow, "bmi_cache"))
        *flowproto = FLOWPROTO_BMI_CACHE;
    else if (!strcmp(flow, "multiqueue"))
        *flowproto = FLOWPROTO_MULTIQUEUE;
    else
    {
        gossip_err("Error: unrecognized flowproto option: %s\n", flow);
        return (-PVFS_EINVAL);
    }

    return (0);
}

void PVFS_sys_free_mntent(
    struct PVFS_sys_mntent *mntent)
{
    if (mntent)
    {
        if (mntent->pvfs_config_server)
        {
            free(mntent->pvfs_config_server);
            mntent->pvfs_config_server = NULL;
        }
        if (mntent->pvfs_fs_name)
        {
            free(mntent->pvfs_fs_name);
            mntent->pvfs_fs_name = NULL;
        }
        if (mntent->mnt_dir)
        {
            free(mntent->mnt_dir);
            mntent->mnt_dir = NULL;
        }
        if (mntent->mnt_opts)
        {
            free(mntent->mnt_opts);
            mntent->mnt_opts = NULL;
        }

        mntent->flowproto = 0;
        mntent->encoding = 0;
        mntent->fs_id = PVFS_FS_ID_NULL;
    }    
}

static int copy_mntent(
    struct PVFS_sys_mntent *dest_mntent,
    struct PVFS_sys_mntent *src_mntent)
{
    int ret = -PVFS_ENOMEM;

    if (dest_mntent && src_mntent)
    {
        memset(dest_mntent, 0, sizeof(struct PVFS_sys_mntent));

        dest_mntent->pvfs_config_server =
            strdup(src_mntent->pvfs_config_server);
        dest_mntent->pvfs_fs_name = strdup(src_mntent->pvfs_fs_name);

        if (src_mntent->mnt_dir)
        {
            dest_mntent->mnt_dir = strdup(src_mntent->mnt_dir);
        }
        if (src_mntent->mnt_opts)
        {
            dest_mntent->mnt_opts = strdup(src_mntent->mnt_opts);
        }
        dest_mntent->flowproto = src_mntent->flowproto;
        dest_mntent->encoding = src_mntent->encoding;
        dest_mntent->fs_id = src_mntent->fs_id;

        /* TODO: memory allocation error handling */
        ret = 0;
    }
    return ret;
}

/*
 * Pull out the wire encoding specified as a mount option in the tab
 * file.
 *
 * Input string is not modified; result goes into et.
 *
 * Returns 0 if all okay.
 */
static int parse_encoding_string(
    const char *cp,
    enum PVFS_encoding_type *et)
{
    const char *cq;
    int i;
    struct
    {
        const char *name;
        enum PVFS_encoding_type val;
    } enc_str[] = {
        { "direct", ENCODING_DIRECT },
        { "le_bfield", ENCODING_LE_BFIELD },
        { "xdr", ENCODING_XDR }
    };

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: input is %s\n",
                 __func__, cp);
    cp += strlen("encoding");
    for (; isspace(*cp); cp++);        /* optional spaces */
    if (*cp != '=')
    {
        gossip_err("Error: %s: malformed encoding option in tab file.\n",
                   __func__);
        return -PVFS_EINVAL;
    }
    for (++cp; isspace(*cp); cp++);        /* optional spaces */
    for (cq = cp; *cq && *cq != ','; cq++);/* find option end */

    *et = -1;
    for (i = 0; i < sizeof(enc_str) / sizeof(enc_str[0]); i++)
    {
        int n = strlen(enc_str[i].name);
        if (cq - cp > n)
            n = cq - cp;
        if (!strncmp(enc_str[i].name, cp, n))
        {
            *et = enc_str[i].val;
            break;
        }
    }
    if (*et == -1)
    {
        gossip_err("Error: %s: unknown encoding type in tab file.\n",
                   __func__);
        return -PVFS_EINVAL;
    }
    return 0;
}

/* PINT_release_pvfstab()
 *
 * frees up any resources associated with previously parsed tabfiles
 *
 * no return value
 */
void PINT_release_pvfstab(void)
{
    int i, j;

    gen_mutex_lock(&s_stat_tab_mutex);
    for(i=0; i<s_stat_tab_count; i++)
    {
        for (j = 0; j < s_stat_tab_array[i].mntent_count; j++)
        {
            PVFS_sys_free_mntent(&s_stat_tab_array[i].mntent_array[j]);
        }
        free(s_stat_tab_array[i].mntent_array);
    }
    s_stat_tab_count = 0;

    for (j = 0; j < s_stat_tab_array[
             PVFS2_DYNAMIC_TAB_INDEX].mntent_count; j++)
    {
        PVFS_sys_free_mntent(
            &s_stat_tab_array[
                PVFS2_DYNAMIC_TAB_INDEX].mntent_array[j]);
    }
    if (s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_array)
    {
        free(s_stat_tab_array[PVFS2_DYNAMIC_TAB_INDEX].mntent_array);
    }

    gen_mutex_unlock(&s_stat_tab_mutex);
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
