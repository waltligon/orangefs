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
 * returns handle value on success, 0 on failure
 */
/* TODO: give this function a way to report error codes */
/* TODO: make uid, gid passed in later */
PVFS_handle PVFS_util_lookup_parent(char *filename, PVFS_fs_id fs_id)
{
    char buf[PVFS_SEGMENT_MAX] = {0};
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_look;

    memset(&resp_look,0,sizeof(PVFS_sysresp_lookup));

    if (PINT_get_base_dir(filename,buf,PVFS_SEGMENT_MAX))
    {
        if (filename[0] != '/')
        {
            gossip_err("Invalid dirname (no leading '/')\n");
        }
        gossip_err("cannot get parent directory of %s\n",filename);
        return (PVFS_handle)0;
    }

    /* retrieve the parent handle */
    credentials.uid = 100;
    credentials.gid = 100;

    if (PVFS_sys_lookup(fs_id, buf, credentials ,&resp_look))
    {
        gossip_err("Lookup failed on %s\n",buf);
        return (PVFS_handle)0;
    }
    return resp_look.pinode_refn.handle;
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
