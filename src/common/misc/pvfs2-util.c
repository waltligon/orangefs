/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "pvfs2-sysint.h"
#include "pvfs2-util.h"
#include "str-utils.h"
#include "gossip.h"

#define PARSER_MAX_LINE_LENGTH 255

/* Function Prototypes */
static int mntlist_new(int num_mnts,pvfs_mntlist *mntlist_ptr);

/* Function: PVFS_util_parse_pvfstab
 *
 * parses the PVFS fstab file
 *
 * returns 0 on success, -1 on error
 */
int PVFS_util_parse_pvfstab(char *filename,pvfs_mntlist *pvfstab_p)
{
	FILE *tab;
	char line[PARSER_MAX_LINE_LENGTH];
	int index = 0, ret = 0,lines = 0;
	size_t linelen=0;
	int i = 0, start = 0, end = 0, num_slashes_seen = 0;

	if (filename == NULL)
	{
	    /* if we didn't get a filename, just look in the current dir for
	     * a file named "pvfstab"
	     */

	    tab = fopen("pvfstab","rb");
	    if (tab == NULL) 
	    {
		return(-1);
	    }
	}
	else
	{
	    tab = fopen(filename,"rb");
	    if (tab == NULL) 
	    {
		return(errno);
	    }
	}

	/* Count the number of lines */
	while (fgets(line,PARSER_MAX_LINE_LENGTH,tab) != NULL)
	{
		/* ignore any blank lines */
		if (strlen(line) > 1)
			lines++;
	}
	if ((ret = mntlist_new(lines,pvfstab_p) < 0)) 
	{
		fclose(tab);
		return(-1);
	}

	fseek(tab, 0, SEEK_SET);

	/* Fill in the mount structure */
	/* Get a line from the pvstab file */
	while (fgets(line,PARSER_MAX_LINE_LENGTH,tab) != NULL)
	{
		/* Is the line blank? */
		linelen = strlen(line);
		if (linelen > 1)
		{

		    /* 'pvfs-tcp://user:port/' */
		    //sscanf("pvfs-%s://%s:%d/%s")
		    for(i = 0; i < linelen; i++)
		    {
			if (line[i] == '-')
			{
			    start = i+1;
			    break;
			}
		    }

		    num_slashes_seen = 0;
		    for(i = start; i < linelen; i++)
		    {
			if (line[i] == '/')
			{
			    num_slashes_seen++;
			    if (num_slashes_seen > 2)
			    {
				end = i;
				break;
			    }
			}
		    }
		    if (end - start < 0)
		    {
			printf("end = %d\nstart = %d\n",end,start);
			ret = -EINVAL;
			goto metaaddr_failure;
		    }
		    pvfstab_p->ptab_p[index].meta_addr = malloc(end - start + 1);
		    if(pvfstab_p->ptab_p[index].meta_addr == NULL)
		    {
			ret = -ENOMEM;
			goto metaaddr_failure;
		    }
		    memcpy(pvfstab_p->ptab_p[index].meta_addr, &line[start], end-start);
		    pvfstab_p->ptab_p[index].meta_addr[end - start] = '\0';

		    start = end + 1; /*skip the '/' character*/
		    for(i = start; i < linelen; i++)
		    {
			if ((line[i] == ' ') || (line[i] == '\t'))
			{
			    end = i;
			    break;
			}
		    }

		    if (end - start < 0)
		    {
			printf("end = %d\nstart = %d\n",end,start);
			ret = -EINVAL;
			goto servmnt_failure;
		    }
		    pvfstab_p->ptab_p[index].service_name = malloc(end - start + 1);
		    if(pvfstab_p->ptab_p[index].service_name == NULL)
		    {
			ret = -ENOMEM;
			goto servmnt_failure;
		    }
		    memcpy(pvfstab_p->ptab_p[index].service_name, &line[start], end-start);
		    pvfstab_p->ptab_p[index].service_name[end - start] = '\0';
		    start = end + 1;
		    for(i = start; i < linelen; i++)
		    {
			if ((line[i] == ' ') || (line[i] == '\t'))
			{
			    end = i;
			    break;
			}
		    }

		    if (end - start < 0)
		    {
			printf("end = %d\nstart = %d\n",end,start);
			ret = -EINVAL;
			goto localmnt_failure;
		    }
		    pvfstab_p->ptab_p[index].local_mnt_dir = malloc(end - start + 1);
		    if(pvfstab_p->ptab_p[index].local_mnt_dir == NULL)
		    {
			ret = -ENOMEM;
			goto localmnt_failure;
		    }
		    memcpy(pvfstab_p->ptab_p[index].local_mnt_dir, &line[start], end-start);
		    pvfstab_p->ptab_p[index].local_mnt_dir[end - start] = '\0';
		    start = end + 1;
		    for(i = start; i < linelen; i++)
		    {
			if ((line[i] == ' ') || (line[i] == '\t'))
			{
			    end = i;
			    break;
			}
		    }

		    if (end - start < 0)
		    {
			printf("end = %d\nstart = %d\n",end,start);
			ret = -EINVAL;
			goto fstype_failure;
		    }
		    pvfstab_p->ptab_p[index].fs_type = malloc(end - start + 1);
		    if(pvfstab_p->ptab_p[index].fs_type == NULL)
		    {
			ret = -ENOMEM;
			goto fstype_failure;
		    }
		    memcpy(pvfstab_p->ptab_p[index].fs_type, &line[start], end-start);
		    pvfstab_p->ptab_p[index].fs_type[end - start] = '\0';
		    start = end + 1;
		    for(i = start; i < linelen; i++)
		    {
			if ((line[i] == ' ') || (line[i] == '\t'))
			{
			    end = i;
			    break;
			}
		    }

		    if (end - start < 0)
		    {
			printf("end = %d\nstart = %d\n",end,start);
			ret = -EINVAL;
			goto opt1_failure;
		    }
		    pvfstab_p->ptab_p[index].opt1 = malloc(end - start + 1);
		    if(pvfstab_p->ptab_p[index].opt1 == NULL)
		    {
			ret = -ENOMEM;
			goto opt1_failure;
		    }
		    memcpy(pvfstab_p->ptab_p[index].opt1, &line[start], end-start);
		    pvfstab_p->ptab_p[index].opt1[end - start] = '\0';
		    start = end + 1;
		    for(i = start; i < linelen; i++)
		    {
			if ((line[i] == ' ') || (line[i] == '\t'))
			{
			    end = i;
			    break;
			}
		    }

		    if (end - start < 0)
		    {
			printf("end = %d\nstart = %d\n",end,start);
			ret = -EINVAL;
			goto opt2_failure;
		    }
		    pvfstab_p->ptab_p[index].opt2 = malloc(end - start + 1);
		    if(pvfstab_p->ptab_p[index].opt2 == NULL)
		    {
			ret = -ENOMEM;
			goto opt2_failure;
		    }
		    memcpy(pvfstab_p->ptab_p[index].opt2, &line[start], end-start);
		    pvfstab_p->ptab_p[index].opt2[end - start] = '\0';

		/* Increment the counter */
		index++;

		}
	}
	
	/* Close the pvfstab file */
	fclose(tab);

	return(0);

/* TODO: if we hit these error cases, we're going to need to free() whatever
 * we were able to malloc before we encountered whatever error happened.
 */

metaaddr_failure:
servmnt_failure:
localmnt_failure:
fstype_failure:
opt1_failure:
opt2_failure:
	fclose(tab);
	PVFS_util_pvfstab_mntlist_free(pvfstab_p);
	return(ret);

}

/* PVFS_util_pvfstab_mntlist_free
 *
 * frees the mount entries data structure
 *
 * does not return anything
 */
void PVFS_util_pvfstab_mntlist_free(pvfs_mntlist *e_p)
{
	int i = 0;
	pvfs_mntlist *mnts = e_p;

	for(i = 0;i < mnts->nr_entry;i++)
	{
		if (mnts->ptab_p[i].meta_addr) 
			free(mnts->ptab_p[i].meta_addr); 
		if (mnts->ptab_p[i].service_name)
			free(mnts->ptab_p[i].service_name);
		if (mnts->ptab_p[i].local_mnt_dir)
			free(mnts->ptab_p[i].local_mnt_dir);
		if (mnts->ptab_p[i].fs_type)
			free(mnts->ptab_p[i].fs_type);
		if (mnts->ptab_p[i].opt1)
			free(mnts->ptab_p[i].opt1);
		if (mnts->ptab_p[i].opt2)
			free(mnts->ptab_p[i].opt2);
	}
	free(mnts->ptab_p);

}

/* PVFS_util_lookup_parent()
 *
 * given a pathname and an fsid, looks up the handle of the parent
 * directory
 *
 * returns 0 on success, -errno on failure
 */
int PVFS_util_lookup_parent(char *filename, PVFS_fs_id fs_id, 
    PVFS_credentials credentials, PVFS_handle* handle)
{
    char buf[PVFS_SEGMENT_MAX] = {0};
    PVFS_sysresp_lookup resp_look;
    int ret = -1;

    memset(&resp_look,0,sizeof(PVFS_sysresp_lookup));

    if (PINT_get_base_dir(filename,buf,PVFS_SEGMENT_MAX))
    {
        if (filename[0] != '/')
        {
            gossip_err("Invalid dirname (no leading '/')\n");
        }
        gossip_err("cannot get parent directory of %s\n",filename);
	/* TODO: use defined name for this */
	*handle = 0;
	return(-EINVAL);
    }

    ret = PVFS_sys_lookup(fs_id, buf, credentials ,&resp_look);
    if (ret < 0)
    {
        gossip_err("Lookup failed on %s\n",buf);
	/* TODO: use defined name for this */
	*handle = 0;
        return(ret);
    }
    *handle = resp_look.pinode_refn.handle;
    return(0);
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
int PVFS_util_remove_base_dir(char *pathname, char *out_dir, int out_max_len)
{
    int ret = -1, len = 0;
    char *start, *end, *end_ref;

    if (pathname && out_dir && out_max_len)
    {
        if ((strcmp(pathname,"/") == 0) || (pathname[0] != '/'))
        {
            return ret;
        }

        start = pathname;
        end = (char *)(pathname + strlen(pathname));
        end_ref = end;

        while(end && (end > start) && (*(--end) != '/'));

        len = (int)((char *)(end_ref - ++end));
        if (len < out_max_len)
        {
            memcpy(out_dir,end,len);
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
int PVFS_util_remove_dir_prefix(char *pathname, char* prefix, char *out_path, 
    int out_max_len)
{
    int ret = -EINVAL;
    int prefix_len, pathname_len;
    int cut_index;

    if(!pathname || !prefix || !out_path || !out_max_len)
    {
	return(-EINVAL);
    }

    /* make sure we are given absolute paths */
    if ((pathname[0] != '/') || (prefix[0] != '/'))
    {
	return ret;
    }

    prefix_len = strlen(prefix);
    pathname_len = strlen(pathname);

    /* account for trailing slashes on prefix */
    while(prefix[prefix_len-1] == '/')
    {
	prefix_len--;
    }

    /* if prefix_len is now zero, then prefix must have been root
     * directory; return copy of entire pathname
     */
    if(prefix_len == 0)
    {
	cut_index = 0;
    }
    else
    {
	
	/* make sure prefix would fit in pathname */
	if(prefix_len > (pathname_len + 1))
	    return(-ENOENT);

	/* see if we can find prefix at beginning of path */
	if(strncmp(prefix, pathname, prefix_len) == 0)
	{
	    /* apparent match; see if next element is a slash */
	    if(pathname[prefix_len] != '/')
		return(-ENOENT);
	    
	    /* this was indeed a match */
	    cut_index = prefix_len;
	}
	else
	{
	    return(-ENOENT);
	}
    }

    /* if we hit this point, then we were successful */

    /* is the buffer large enough? */
    if((1+strlen(&(pathname[cut_index]))) > out_max_len)
	return(-ENAMETOOLONG);

    /* copy out appropriate part of pathname */
    strcpy(out_path, &(pathname[cut_index]));
    return(0);
}

/***********************/

/* mntlist_new
 *
 * allocates memory for an array of pvfs mount entries
 *
 * returns 0 on success, -1 on failure
 */
static int mntlist_new(int num_mnts,pvfs_mntlist *mntlist_ptr)
{
	mntlist_ptr->nr_entry = num_mnts;
	mntlist_ptr->ptab_p = malloc(num_mnts * sizeof(pvfs_mntent));
	if (!mntlist_ptr->ptab_p)
	{
		return(-ENOMEM);
	}
	memset(mntlist_ptr->ptab_p, 0, num_mnts*sizeof(pvfs_mntent));

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
