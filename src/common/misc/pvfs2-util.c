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

#include "pvfs2-config.h"
#include "pvfs2-sysint.h"
#include "pvfs2-util.h"
#include "str-utils.h"
#include "pvfs2-debug.h"
#include "gossip.h"

/* TODO: add replacement functions for systems without getmntent() */
#ifndef HAVE_GETMNTENT
    #error HAVE_GETMNTENT undefined! needed for parse_pvfstab
#endif

#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif

static int parse_flowproto_string(const char* input, enum PVFS_flowproto_type* 
    flowproto);

/* PVFS_util_parse_pvfstab()
 *
 * parses either the file pointed to by the PVFS2TAB_FILE env variable,
 * or /etc/fstab, or /etc/pvfs2tab or ./pvfs2tab to extract pvfs2 mount 
 * entries.
 *
 * example entry:
 * tcp://localhost:3334/pvfs2-fs /mnt/pvfs2 pvfs2 defaults 0 0
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PVFS_util_parse_pvfstab(pvfs_mntlist* pvfstab_p)
{
    FILE* mnt_fp = NULL;
    int file_count = 4;
    char* file_list[4] = {NULL, "/etc/fstab", "/etc/pvfs2tab", "pvfs2tab"};
    char* targetfile = NULL;
    struct mntent* tmp_ent;
    int i;
    int slashcount = 0;
    char* slash = NULL;
    char* last_slash = NULL;
    int ret = -1;

    /* safety */
    pvfstab_p->ptab_count = 0;

    /* first check for environment variable override */
    file_list[0] = getenv("PVFS2TAB_FILE");

    /* scan our prioritized list of tab files in order, stop when we find
     * one that has at least one pvfs2 entry
     */
    for(i=0; (i<file_count && !targetfile); i++)
    {
	mnt_fp = setmntent(file_list[i], "r");
	if(mnt_fp)
	{
	    while((tmp_ent = getmntent(mnt_fp)))
	    {
		if(strcmp(tmp_ent->mnt_type, "pvfs2") == 0)
		{
		    targetfile = file_list[i];
		    pvfstab_p->ptab_count++;
		}
	    }
	    endmntent(mnt_fp);
	}
    }

    /* print some debugging information */
    if(!targetfile)
    {
	gossip_lerr("Error: could not find any pvfs2 tabfile entries.\n");
	return(-PVFS_ENOENT);
    }
    gossip_debug(CLIENT_DEBUG, "Using pvfs2 tab file: %s\n", targetfile);

    /* allocate array of entries */
    pvfstab_p->ptab_array = (struct pvfs_mntent*)malloc(pvfstab_p->ptab_count *
	sizeof(struct pvfs_mntent));
    if(!pvfstab_p->ptab_array)
	return(-PVFS_ENOMEM);
    memset(pvfstab_p->ptab_array, 0, 
	pvfstab_p->ptab_count*sizeof(struct pvfs_mntent));

    /* reopen our chosen fstab file */
    mnt_fp = setmntent(targetfile, "r");
    /* this shouldn't fail - we just opened it earlier */
    assert(mnt_fp);

    /* scan through looking for every pvfs2 entry */
    i = 0;
    while((tmp_ent = getmntent(mnt_fp)))
    {
	if(strcmp(tmp_ent->mnt_type, "pvfs2") == 0)
	{
	    /* sanity check */
	    slash = tmp_ent->mnt_fsname;
	    slashcount = 0;
	    while((slash = index(slash, '/')))
	    {
		slash++;
		slashcount++;
	    }

	    /* find a reference point in the string */
	    last_slash = rindex(tmp_ent->mnt_fsname, '/');

	    if(slashcount != 3)
	    {
		gossip_lerr("Error: invalid tab file entry: %s\n",
		    tmp_ent->mnt_fsname);
		endmntent(mnt_fp);
		return(-PVFS_EINVAL);
	    }
	
	    /* allocate room for our copies of the strings */
	    pvfstab_p->ptab_array[i].pvfs_config_server = 
		(char*)malloc(strlen(tmp_ent->mnt_fsname) + 1);
	    pvfstab_p->ptab_array[i].mnt_dir = 
		(char*)malloc(strlen(tmp_ent->mnt_dir) + 1);
	    pvfstab_p->ptab_array[i].mnt_opts = 
		(char*)malloc(strlen(tmp_ent->mnt_opts) + 1);

	    /* bail if any mallocs failed */
	    if(!pvfstab_p->ptab_array[i].pvfs_config_server
		|| !pvfstab_p->ptab_array[i].mnt_dir
		|| !pvfstab_p->ptab_array[i].mnt_opts)
	    {
		/* TODO: clean up mallocs */
		endmntent(mnt_fp);
		return(-PVFS_EINVAL);
	    }

	    /* make our own copy of parameters of interest */

	    /* config server and fs name are a special case, take one 
	     * string and split it in half on "/" delimiter
	     */
	    strcpy(pvfstab_p->ptab_array[i].pvfs_config_server,
		tmp_ent->mnt_fsname);
	    *last_slash = '\0';
	    last_slash++;
	    pvfstab_p->ptab_array[i].pvfs_fs_name = last_slash;

	    /* mnt_dir and mnt_opts are verbatim copies */
	    strcpy(pvfstab_p->ptab_array[i].mnt_dir,
		tmp_ent->mnt_dir);
	    strcpy(pvfstab_p->ptab_array[i].mnt_opts,
		tmp_ent->mnt_opts);

	    /* find out if a particular flow protocol was specified */
	    if((hasmntopt(tmp_ent, "flowproto")))
	    {
		ret = parse_flowproto_string(tmp_ent->mnt_opts,
		    &(pvfstab_p->ptab_array[i].flowproto));
		if(ret < 0)
		{
		    /* TODO: clean up mallocs */
		    endmntent(mnt_fp);
		    return(ret);
		}
	    }
	    else
	    {
		pvfstab_p->ptab_array[i].flowproto = FLOWPROTO_DEFAULT;
	    }
	    i++;
	}
    }

    return(0);
}

/* PVFS_util_pvfstab_mntlist_free
 *
 * frees the mount entries data structure
 *
 * does not return anything
 */
void PVFS_util_free_pvfstab(
    pvfs_mntlist * e_p)
{
    int i = 0;

    for(i=0; i<e_p->ptab_count; i++)
    {
	free(e_p->ptab_array[i].pvfs_config_server);
	free(e_p->ptab_array[i].mnt_dir);
	free(e_p->ptab_array[i].mnt_opts);
    }

    free(e_p->ptab_array);
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

    ret = PVFS_sys_lookup(fs_id, buf, credentials, &resp_look);
    if (ret < 0)
    {
	gossip_err("Lookup failed on %s\n", buf);
	/* TODO: use defined name for this */
	*handle = 0;
	return (ret);
    }
    *handle = resp_look.pinode_refn.handle;
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

	len = (int) ((char *) (end_ref - ++end));
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
    char *pathname,
    char *prefix,
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
	    if (pathname[prefix_len] != '/')
		return (-ENOENT);

	    /* this was indeed a match */
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

    /* copy out appropriate part of pathname */
    strcpy(out_path, &(pathname[cut_index]));
    return (0);
}

/* parse_flowproto_string()
 *
 * looks in the mount options string for a flowprotocol specifier and 
 * sets the flowproto type accordingly
 *
 * returns 0 on success, -PVFS_error on failure
 */
static int parse_flowproto_string(const char* input, enum PVFS_flowproto_type* 
    flowproto)
{
    int ret = 0;
    char* start = NULL;
    char flow[256];
    char* comma = NULL;

    start = strstr(input, "flowproto");
    /* we must find a match if this function is being called... */
    assert(start);

    /* scan out the option */
    ret = sscanf(start, "flowproto = %255s ,", flow);
    if(ret != 1)
    {
	gossip_err("Error: malformed flowproto option in tab file.\n");
	return(-PVFS_EINVAL);
    }

    /* chop it off at any trailing comma */
    comma = index(flow, ',');
    if(comma)
    {
	comma[0] = '\0';
    }

    if(!strcmp(flow, "bmi_trove"))
	*flowproto = FLOWPROTO_BMI_TROVE;
    else if(!strcmp(flow, "dump_offsets"))
	*flowproto = FLOWPROTO_DUMP_OFFSETS;
    else if(!strcmp(flow, "bmi_cache"))
	*flowproto = FLOWPROTO_BMI_CACHE;
    else
    {
	gossip_err("Error: unrecognized flowproto option: %s\n", flow);
	return(-PVFS_EINVAL);
    }

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
