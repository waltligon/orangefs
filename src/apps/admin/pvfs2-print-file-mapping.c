/*
 * Author julian M. Kunkel
 * (C) 2001 Clemson University and The University of Chicago
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <libgen.h>
#include <getopt.h>

#include "pvfs2.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"
#include "pvfs2-internal.h"

static void usage(
    int argc,
    char **argv);

int lookup(
    char *pvfs2_file,
    PVFS_credentials * credentials,
    PVFS_fs_id * out_fs_id,
    PVFS_object_ref * out_object_ref,
    PVFS_sysresp_getattr * out_resp_getattr);
int print_server_mapping(
    PVFS_fs_id fs_id,
    PVFS_credentials * credentials,
    PVFS_object_ref metafile_ref,
    PVFS_sysresp_getattr * resp_meta_getattr);
int getServers(
    int type,
    PVFS_fs_id fsid,
    PVFS_credentials * credentials,
    PVFS_BMI_addr_t ** out_addr_array_p,
    char ***out_server_names,
    int *out_server_count,
    PVFS_handle ** lower_handle,
    PVFS_handle ** upper_handle);
const char *get_server_for_handle(
    PVFS_handle handle,
    int server_count,
    char **server_names,
    PVFS_handle * data_lower_handle,
    PVFS_handle * data_upper_handle);

#define MAX_SERVER_NAME 100

int getServers(
    int type,
    PVFS_fs_id fsid,
    PVFS_credentials * credentials,
    PVFS_BMI_addr_t ** out_addr_array_p,
    char ***out_server_names,
    int *out_server_count,
    PVFS_handle ** lower_handle_p,
    PVFS_handle ** upper_handle_p)
{
    int ret, i;
    int count;
    PVFS_BMI_addr_t *addr_array;

    ret = PVFS_mgmt_count_servers(fsid, credentials, type, &count);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_count_servers()", ret);
        exit(1);
    }

    *out_server_count = count;

    addr_array = (PVFS_BMI_addr_t *) malloc(count * sizeof(PVFS_BMI_addr_t));
    memset(addr_array, 0, count * sizeof(PVFS_BMI_addr_t));
    *out_addr_array_p = addr_array;

    *out_server_names = (char **) malloc(count * sizeof(char *));
    *lower_handle_p = (PVFS_handle *) malloc(count * sizeof(PVFS_handle));
    *upper_handle_p = (PVFS_handle *) malloc(count * sizeof(PVFS_handle));

    if (addr_array == NULL || *out_server_names == NULL ||
        *lower_handle_p == NULL || *upper_handle_p == NULL)
    {
        perror("malloc");
        return ret;
    }

    ret =
        PVFS_mgmt_get_server_array(fsid, credentials, type, addr_array,
                                   out_server_count);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_get_server_array()", ret);
        return ret;
    }

    if (count != *out_server_count)
    {
        fprintf(stderr, "Error count != *out_server_count: %d != %d \n", count,
                *out_server_count);
        return -1;
    }

    char **server_names = *out_server_names;
    PVFS_handle *lower_handle = *lower_handle_p;
    PVFS_handle *upper_handle = *upper_handle_p;

    for (i = 0; i < count; i++)
    {
        ret = PVFS_mgmt_map_addr_to_alias(fsid, credentials, addr_array[i],
                                          &server_names[i],
                                          &lower_handle[i], &upper_handle[i],
                                          type);
        if (ret != 0)
        {
            fprintf(stderr,
                    "Could not map bmi_address to string for entry: %d\n",
                    i + 1);
            exit(1);
        }
    }

    return 0;
}


const char *get_server_for_handle(
    PVFS_handle handle,
    int server_count,
    char **server_names,
    PVFS_handle * data_lower_handle,
    PVFS_handle * data_upper_handle)
{
    int i;
    for (i = 0; i < server_count; i++)
    {
        if (data_lower_handle[i] <= handle && handle <= data_upper_handle[i])
        {
            return server_names[i];
        }
    }
    return NULL;
}

int print_server_mapping(
    PVFS_fs_id fsid,
    PVFS_credentials * credentials,
    PVFS_object_ref metafile_ref,
    PVFS_sysresp_getattr * resp_meta_getattr)
{
    int meta_server_count;
    PVFS_BMI_addr_t *meta_addr_array = NULL;
    char **meta_server_names = NULL;
    PVFS_handle *meta_lower_handle = NULL;
    PVFS_handle *meta_upper_handle = NULL;

    int data_server_count;
    PVFS_BMI_addr_t *data_addr_array = NULL;
    char **data_server_names = NULL;
    PVFS_handle *data_lower_handle = NULL;
    PVFS_handle *data_upper_handle = NULL;
    int ret;
    int i;
    int dfile_count;
    PVFS_sys_attr *attr;
    PVFS_handle *dfile_array;

    ret = getServers(PVFS_MGMT_META_SERVER,
                     fsid,
                     credentials,
                     &meta_addr_array,
                     &meta_server_names,
                     &meta_server_count,
                     &meta_lower_handle, &meta_upper_handle);

    if (ret < 0)
    {
        return ret;
    }

    ret = getServers(PVFS_MGMT_IO_SERVER,
                     fsid,
                     credentials,
                     &data_addr_array,
                     &data_server_names,
                     &data_server_count,
                     &data_lower_handle, &data_upper_handle);
    if (ret < 0)
    {
        return ret;
    }

    attr = &resp_meta_getattr->attr;

    /*
     * get datafiles from acache if possible !
     */

    dfile_array =
        (PVFS_handle *) malloc(sizeof(PVFS_handle) * attr->dfile_count);
    dfile_count = attr->dfile_count;
    ret =
        PVFS_mgmt_get_datafiles_from_acache(metafile_ref, dfile_array,
                                            &dfile_count);

    if (ret < 0)
    {
        free(dfile_array);
        fprintf(stderr,
                "Datafiles could not be obtained from acache, maybe acache freed information (change timeout)\n");
        return ret;
    }

    if (meta_server_count != 1)
    {
        free(dfile_array);
        fprintf(stderr, "Error, file has multiple metadataservers: %d\n",
                meta_server_count);
        return -1;
    }

    printf("Metafile: \n\thandle: %d server:%s\n", metafile_ref.fs_id,
           get_server_for_handle(metafile_ref.fs_id,
                                 meta_server_count, meta_server_names,
                                 meta_lower_handle, meta_upper_handle));
    printf("Datafiles: %d\n", attr->dfile_count);
    for (i = 0; i < attr->dfile_count; i++)
    {
        const char *alias;
        alias = get_server_for_handle(dfile_array[i],
                                      data_server_count, data_server_names,
                                      data_lower_handle, data_upper_handle);

        printf("\thandle:%lld server:%s\n", lld(dfile_array[i]), alias);
    }

    free(dfile_array);
    return 0;
}


int main(
    int argc,
    char **argv)
{
    int64_t ret;
    PVFS_credentials credentials;
    PVFS_sysresp_getattr meta_attr;
    PVFS_fs_id fsid;
    PVFS_object_ref metafile_ref;

    if (argc < 2 || strcmp(argv[1], "-h") == 0)
    {
        usage(argc, argv);
        exit(1);
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_init_defaults", ret);
        return (-1);
    }

    PVFS_util_gen_credentials(&credentials);


    if (lookup(argv[1], &credentials, &fsid, &metafile_ref, &meta_attr) != 0)
    {
        return -1;
    }

    /*
     * Got metafile 
     */


    if (print_server_mapping(fsid, &credentials, metafile_ref, &meta_attr) != 0)
    {
        return -1;
    }


    PVFS_sys_finalize();
    return (ret);
}

static void usage(
    int argc,
    char **argv)
{
    fprintf(stderr, "Usage: %s file \n", argv[0]);
    return;
}


int lookup(
    char *pvfs2_file,
    PVFS_credentials * credentials,
    PVFS_fs_id * out_fs_id,
    PVFS_object_ref * out_object_ref,
    PVFS_sysresp_getattr * out_resp_getattr)
{
    char *entry_name;           /* relativ name of the pvfs2 file */
    char str_buf[PVFS_NAME_MAX];        /* basename of pvfs2 file */
    char file[PVFS_NAME_MAX];
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref parent_ref;
    int fsid;
    int64_t ret;

    strncpy(file, pvfs2_file, PVFS_NAME_MAX);

    /*
     * Step1: lookup filesystem 
     */
    ret = PVFS_util_resolve(file, &(fsid), file, PVFS_NAME_MAX);
    if (ret < 0)
    {
        fprintf(stderr, "PVFS_util_resolve error\n");
        return -1;
    }
    *out_fs_id = fsid;

    /*
     * ripped from pvfs2-cp.c lookup filename
     */
    entry_name = str_buf;
    if (strcmp(file, "/") == 0)
    {
        /* special case: PVFS2 root file system, so stuff the end of
         * srcfile onto pvfs2_path */
        char *segp = NULL, *prev_segp = NULL;
        void *segstate = NULL;

        /* can only perform this special case if we know srcname */
        if (file == NULL)
        {
            fprintf(stderr, "unable to guess filename in " "toplevel PVFS2\n");
            return -1;
        }

        memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
        ret = PVFS_sys_lookup(fsid, file,
                              credentials, &resp_lookup,
                              PVFS2_LOOKUP_LINK_FOLLOW, NULL);
        if (ret < 0)
        {
            PVFS_perror("PVFS_sys_lookup", ret);
            return (-1);
        }

        while (!PINT_string_next_segment(file, &segp, &segstate))
        {
            prev_segp = segp;
        }
        entry_name = prev_segp; /* see... points to basename of srcname */
    }
    else        /* given either a pvfs2 directory or a pvfs2 file */
    {
        /* get the absolute path on the pvfs2 file system */

        /*parent_ref.fs_id = obj->pvfs2.fs_id; */

        if (PINT_remove_base_dir(file, str_buf, PVFS_NAME_MAX))
        {
            if (file[0] != '/')
            {
                fprintf(stderr, "Error: poorly formatted path.\n");
            }
            fprintf(stderr, "Error: cannot retrieve entry name for "
                    "creation on %s\n", file);
            return (-1);
        }
        ret = PINT_lookup_parent(file, fsid, credentials, &parent_ref.handle);
        if (ret < 0)
        {
            PVFS_perror("PVFS_util_lookup_parent", ret);
            return (-1);
        }
        else    /* parent lookup succeeded. if the pvfs2 path is just a
                   directory, use basename of src for the new file */
        {
            int len = strlen(file);
            if (file[len - 1] == '/')
            {
                char *segp = NULL, *prev_segp = NULL;
                void *segstate = NULL;

                if (file == NULL)
                {
                    fprintf(stderr, "unable to guess filename\n");
                    return (-1);
                }
                while (!PINT_string_next_segment(file, &segp, &segstate))
                {
                    prev_segp = segp;
                }
                strncat(file, prev_segp, PVFS_NAME_MAX);
                entry_name = prev_segp;
            }
            parent_ref.fs_id = fsid;
        }
    }

    memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
    ret = PVFS_sys_ref_lookup(parent_ref.fs_id, entry_name,
                              parent_ref, credentials, &resp_lookup,
                              PVFS2_LOOKUP_LINK_FOLLOW, NULL);
    if (ret)
    {
        fprintf(stderr, "Failed to lookup object: %s\n", entry_name);
        return -1;
    }

    *out_object_ref = resp_lookup.ref;

    memset(out_resp_getattr, 0, sizeof(PVFS_sysresp_getattr));
    ret = PVFS_sys_getattr(*out_object_ref, PVFS_ATTR_SYS_ALL_NOHINT,
                           credentials, out_resp_getattr, NULL);
    if (ret)
    {
        fprintf(stderr, "Failed to do pvfs2 getattr on %s\n", entry_name);
        return -1;
    }

    if (out_resp_getattr->attr.objtype != PVFS_TYPE_METAFILE)
    {
        fprintf(stderr, "Object is no file ! %s\n", entry_name);
        return -1;
    }

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
