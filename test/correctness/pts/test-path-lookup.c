/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "mpi.h"
#include "pts.h"
#include "pvfs-helper.h"
#include "pvfs2-util.h"
#include "test-path-lookup.h"

extern pvfs_helper_t pvfs_helper;

int build_nested_path(int levels, char *format, int rank, int test_symlinks)
{
    int ret = -1, i = 0;
    char cur_filename[64] = {0}, tmp_buf[PVFS_NAME_MAX] = {0};
    PVFS_fs_id cur_fs_id = 0;
    PVFS_sys_attr attr;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup lookup_resp;
    PVFS_sysresp_mkdir mkdir_resp;
    char PATH_LOOKUP_BASE_DIR[64] = {0};
    PVFS_pinode_reference root_refn, parent_refn, base_refn;
    PVFS_pinode_reference *newdir_refns = NULL;
    PVFS_pinode_reference *lookup_refns = NULL;
    char **absolute_paths = NULL;

    if (levels && format)
    {
        snprintf(PATH_LOOKUP_BASE_DIR, 64, "d%sr%d",
                 format, rank);

        cur_fs_id = pvfs_helper.resp_init.fsid_list[0];

        /* look up the root handle */
        ret = PVFS_sys_lookup(
            cur_fs_id, "/", credentials, &lookup_resp,
            PVFS2_LOOKUP_LINK_NO_FOLLOW);
        if (ret < 0)
        {
            fprintf(stderr," *** lookup failed on root directory\n");
            return ret;
        }

        root_refn = lookup_resp.pinode_refn;
        fprintf(stderr,"Got Root Handle %Lu on fs %d\n",
                Lu(root_refn.handle), root_refn.fs_id);

        attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
        attr.owner = getuid();
        attr.group = getgid();
        attr.perms = 1877;
        attr.atime = attr.ctime = attr.mtime = time(NULL);
        credentials.uid = attr.owner;
        credentials.gid = attr.group;

        /* make the top-level base directory */
        fprintf(stderr," Creating base directory %s under %Lu, %d\n",
                PATH_LOOKUP_BASE_DIR, Lu(root_refn.handle),
                root_refn.fs_id);
        ret = PVFS_sys_mkdir(PATH_LOOKUP_BASE_DIR, root_refn,
                             attr, credentials, &mkdir_resp);
        if (ret < 0)
        {
            fprintf(stderr," PVFS_sys_mkdir failed to create "
                    "base directory %s\n", PATH_LOOKUP_BASE_DIR);
            goto cleanup;
        }
        base_refn = mkdir_resp.pinode_refn;

        newdir_refns = (PVFS_pinode_reference *)malloc(
            (levels * sizeof(PVFS_pinode_reference)));
        lookup_refns = (PVFS_pinode_reference *)malloc(
            (levels * sizeof(PVFS_pinode_reference)));
        absolute_paths = (char **)malloc(
            (levels * PVFS_NAME_MAX * sizeof(char)));
        if (!newdir_refns || !lookup_refns | !absolute_paths)
        {
            fprintf(stderr," failed to allocate reference arrays\n");
            goto cleanup;
        }

        for(i = 0; i < levels; i++)
        {
            parent_refn = mkdir_resp.pinode_refn;

            snprintf(cur_filename, 64, "%s%dr%d", format, i, rank);
            fprintf(stderr,"  Creating directory %s under %Lu, %d\n",
                    cur_filename, Lu(parent_refn.handle),
                    parent_refn.fs_id);

            ret = PVFS_sys_mkdir(cur_filename, parent_refn, attr,
                                 credentials, &mkdir_resp);
            if (ret < 0)
            {
                fprintf(stderr," PVFS_sys_mkdir failed to create "
                        "the directory %s\n", cur_filename);
                goto cleanup;
            }

            /* grab refn of newly created directory */
            newdir_refns[i] = mkdir_resp.pinode_refn;
        }

        /* generate the absolute path names */
        snprintf(tmp_buf, PVFS_NAME_MAX, "/%s", PATH_LOOKUP_BASE_DIR);
        for(i = 0; i < levels; i++)
        {
            snprintf(cur_filename, 64, "/%s%dr%d", format, i, rank);
            strncat(tmp_buf, cur_filename, PVFS_NAME_MAX);
            absolute_paths[i] = strdup(tmp_buf);
            if (strlen(absolute_paths[i]) > PVFS_NAME_MAX)
            {
                fprintf(stderr," Generated pathname is too long to "
                        "be a valid PVFS2 path name\n");
                fprintf(stderr,"%s", absolute_paths[i]);
                goto cleanup;
            }
        }

        /* for each directory just created, do a lookup on them */
        parent_refn = base_refn;
        for(i = 0; i < levels; i++)
        {
            snprintf(cur_filename, 64, "%s%dr%d", format, i, rank);
            fprintf(stderr,
                    " - Looking up relative path %s under %Lu, %d\n",
                    cur_filename, Lu(parent_refn.handle),
                    parent_refn.fs_id);

            /* first do a relative lookup */
            ret = PVFS_sys_ref_lookup(
                parent_refn.fs_id, cur_filename,
                parent_refn, credentials,
                &lookup_resp, PVFS2_LOOKUP_LINK_NO_FOLLOW);
            if (ret < 0)
            {
                fprintf(stderr," PVFS_sys_ref_lookup failed\n");
                goto cleanup;
            }

            /* grab refn of looked up directory */
            lookup_refns[i] = lookup_resp.pinode_refn;

            /* then do an absolute path lookup */
            fprintf(stderr," - Looking up absolute path:\n%s\n",
                    absolute_paths[i]);
            ret = PVFS_sys_lookup(cur_fs_id, absolute_paths[i],
                                  credentials, &lookup_resp,
                                  PVFS2_LOOKUP_LINK_NO_FOLLOW);
            if (ret < 0)
            {
                fprintf(stderr," PVFS_sys_lookup failed\n");
                goto cleanup;
            }

            /*
              assert that the ref lookup and the absolute
              lookup yielded the same result
            */
            if ((lookup_refns[i].fs_id !=
                 lookup_resp.pinode_refn.fs_id) ||
                (lookup_refns[i].handle !=
                 lookup_resp.pinode_refn.handle))
            {
                fprintf(stderr," PVFS_sys_ref_lookup and "
                        "PVFS_sys_lookup returned different results "
                        "when they should be the same!\n");
                goto cleanup;
            }
            parent_refn = lookup_resp.pinode_refn;
        }
        ret = 0;
    }

  cleanup:
    if (absolute_paths)
    {
        for(i = (levels - 1); i > -1; i--)
        {
            parent_refn = ((i == 0) ? base_refn : lookup_refns[i - 1]);
            snprintf(cur_filename, 64, "%s%dr%d", format, i, rank);
            fprintf(stderr,"Removing path %s under %Lu,%d ... ",
                    cur_filename, Lu(parent_refn.handle),
                    parent_refn.fs_id);
            ret = PVFS_sys_remove(cur_filename, parent_refn, credentials);
            fprintf(stderr, "%s\n", ((ret < 0) ? "FAILED" : "DONE"));
            if (ret)
            {
                PVFS_perror("Path removal status: ", ret);
            }
        }
        free(absolute_paths);
    }
    ret = PVFS_sys_remove(PATH_LOOKUP_BASE_DIR, root_refn, credentials);
    if (ret)
    {
        PVFS_perror("Top-level Path removal error ", ret);
    }
    if (newdir_refns)
    {
        free(newdir_refns);
    }
    if (lookup_refns)
    {
        free(lookup_refns);
    }
    return ret;
}

int test_path_lookup(MPI_Comm *comm, int rank, char *buf, void *params)
{
    int ret = -1;
    char *format_prefix1 = "pt-0";
    char *format_prefix2 = "t0";

    if (!pvfs_helper.initialized && initialize_sysint())
    {
        fprintf(stderr, "initialize_sysint failed\n");
        return ret;
    }

    ret = build_nested_path(5, format_prefix1, rank, 0);
    if (ret)
    {
        fprintf(stderr,"(1) Failed to build nested path\n");
        goto error_exit;
    }

    ret = build_nested_path(13, format_prefix2, rank, 0);
    if (ret)
    {
        fprintf(stderr,"(2) Failed to build nested path\n");
        goto error_exit;
    }

  error_exit:
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
