/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <pint-userlib.h>

#define PARSER_MAX_LINE_LENGTH 255

/* Function Prototypes */
static int mntlist_new(int num_mnts,pvfs_mntlist *mntlist_ptr);

/* Function: parse_pvfstab
 *
 * parses the PVFS fstab file
 *
 * returns 0 on success, -1 on error
 */
int parse_pvfstab(char *filename,pvfs_mntlist *pvfstab_p)
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
			goto metaaddr_failure;
		    }
		    pvfstab_p->ptab_p[index].meta_addr = malloc(end - start + 1);
		    if(pvfstab_p->ptab_p[index].meta_addr == NULL)
		    {
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
			goto servmnt_failure;
		    }
		    pvfstab_p->ptab_p[index].service_name = malloc(end - start + 1);
		    if(pvfstab_p->ptab_p[index].service_name == NULL)
		    {
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
			goto localmnt_failure;
		    }
		    pvfstab_p->ptab_p[index].local_mnt_dir = malloc(end - start + 1);
		    if(pvfstab_p->ptab_p[index].local_mnt_dir == NULL)
		    {
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
			goto fstype_failure;
		    }
		    pvfstab_p->ptab_p[index].fs_type = malloc(end - start + 1);
		    if(pvfstab_p->ptab_p[index].fs_type == NULL)
		    {
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
			goto opt1_failure;
		    }
		    pvfstab_p->ptab_p[index].opt1 = malloc(end - start + 1);
		    if(pvfstab_p->ptab_p[index].opt1 == NULL)
		    {
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
			goto opt2_failure;
		    }
		    pvfstab_p->ptab_p[index].opt2 = malloc(end - start + 1);
		    if(pvfstab_p->ptab_p[index].opt2 == NULL)
		    {
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
	free_pvfstab_entry(pvfstab_p);
	return(ret);

}

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
	/* Init the mutex lock */
	/*mntlist_ptr->mt_lock = gen_mutex_build(); */

	return(0);
}

/* free_pvfstab_entry
 *
 * frees the mount entries data structure
 *
 * does not return anything
 */
void free_pvfstab_entry(pvfs_mntlist *e_p)
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

/* search_pvfstab 
 *
 * Search pvfstab for a mount point that matches the file name 
 * 
 * returns 0 on success, -1 on failure
 */
int search_pvfstab(char *fname, pvfs_mntlist mnt, pvfs_mntent *mntent)
{
	int i = 0, lmnt_len = 0, smnt_len = 0, ret = 0;
	
	/* Grab the mutex */
	//gen_mutex_lock(mnt.mt_lock);

	for(i = 0; i < mnt.nr_entry; i++)
	{
		lmnt_len = strlen(mnt.ptab_p[i].local_mnt_dir);
		if (!strncmp(mnt.ptab_p[i].local_mnt_dir,fname,lmnt_len))
		{
			smnt_len = strlen(mnt.ptab_p[i].service_name);
			mntent->service_name = (char *)malloc(smnt_len + 1);
			if (!mntent->service_name)
			{
				ret = -ENOMEM;
				goto unlock_exit;
			}
			strncpy(mntent->service_name,mnt.ptab_p[i].service_name,smnt_len);
			mntent->service_name[smnt_len] = '\0';
			mntent->local_mnt_dir = (char *)malloc(lmnt_len + 1);
			if (!mntent->local_mnt_dir)
			{
				ret = -ENOMEM;
				goto unlock_exit;
			}
			strncpy(mntent->local_mnt_dir,mnt.ptab_p[i].local_mnt_dir,lmnt_len);
			mntent->local_mnt_dir[lmnt_len] = '\0';
			ret = 0;
			goto unlock_exit;
		}

	}
unlock_exit:
	/* Release the mutex */
	//gen_mutex_unlock(mnt.mt_lock);

	return(ret);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
