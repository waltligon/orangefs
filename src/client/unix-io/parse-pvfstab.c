/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <pint-userlib.h>

/* Function Prototypes */
static int mntlist_new(int num_mnts,pvfs_mntlist *mntlist_ptr);

/* Function: parse_pvfstab
 *
 * parses the PVFS fstab file
 *
 * returns 0 on success, -1 on error
 */
int parse_pvfstab(char *fn,pvfs_mntlist *pvfstab_p)
{
	FILE *tab;
	char line[80];
	char *root_mnt = NULL,*tok = NULL;
	char delims[] = "- \n"; /* Delimiters for strtok */
	int index = 0, ret = 0,lines = 0;
	size_t len1=0, len2=0;

	/* Open the pvfstab file */
	tab = fopen("pvfstab","rb");
	if (!tab) 
	{
		return(-1);
	}

	/* Count the number of lines */
	while (fgets(line,80,tab) != NULL)
	{
		if (strlen(line) > 1)
			lines++;
	}
	if ((ret = mntlist_new(lines,pvfstab_p) < 0)) 
	{
		fclose(tab);
		return(-1);
	}

	/* Grab the mutex */

	/* Rewind the file */
	rewind(tab);

	/* Fill in the mount structure */
	/* Get a line from the pvstab file */
	while (fgets(line,80,tab) != NULL)
	{
		//printf("read a line \n");
		/* Is the line blank? */
		if (strlen(line) > 1)
		{
			//printf("line = \"%s\"\n",line);
			/* Skip the first token */
			tok = strtok(line,delims);
			//printf("skipping = %s\n", tok);

			/* Extract the bmi address */
			tok = strtok(NULL,delims);
			//printf("bmi_address = %s\n", tok);
#define META_ADDR pvfstab_p->ptab_p[index].meta_addr 
			root_mnt = strrchr(tok,'/');
			//printf("root = %s\n", root_mnt);
			//printf("tok = %s\n", tok);
			len1 = strlen(tok);
			len2 = strlen(root_mnt);
			if ((len1 < 0) || (len2 < 0))
			{
				ret = -EINVAL;
				goto metaaddr_failure;
			}
			META_ADDR = (PVFS_string)malloc(len1-len2 + 1);
			if (!META_ADDR)
			{
				ret = -ENOMEM;
				goto metaaddr_failure;
			}
			strncpy(META_ADDR,tok,len1-len2);
			META_ADDR[len1 - len2] = '\0';
			//printf("meta_attr = %s\n",META_ADDR);
#undef META_ADDR 

#define SERV_MNT pvfstab_p->ptab_p[index].service_name 
			/* Extract the Root Mount Point */
			len1 = strlen(root_mnt);
			if (len1 < 0)
			{
				ret = -EINVAL;
				goto servmnt_failure;
			}
			SERV_MNT = (PVFS_string)malloc(len1 + 1);
			if (!SERV_MNT)
			{
				ret = -ENOMEM;
				goto servmnt_failure;
			}
			strncpy(SERV_MNT,root_mnt,len1);
			SERV_MNT[len1] = '\0';
#undef SERV_MNT 

#define LOCAL_MNT pvfstab_p->ptab_p[index].local_mnt_dir 
			/* Extract the Local Mount Point */
			tok = strtok(NULL,delims);
			len1 = strlen(tok);
			if (len1 < 0)
			{
				ret = -EINVAL;
				goto localmnt_failure;
			}
			LOCAL_MNT = (PVFS_string)malloc(len1 + 1);
			if (!LOCAL_MNT)
			{
				ret = -ENOMEM;
				goto localmnt_failure;
			}
			strncpy(LOCAL_MNT,tok,len1);
			LOCAL_MNT[len1] = '\0';
#undef LOCAL_MNT 

#define FTYPE pvfstab_p->ptab_p[index].fs_type 
			/* Extract the File System Type */
			tok = strtok(NULL,delims);
			len1 = strlen(tok);
			if (len1 < 0)
			{
				ret = -EINVAL;
				goto fstype_failure;
			}
			FTYPE = (PVFS_string)malloc(len1 + 1);
			if (!FTYPE)
			{
				ret = -ENOMEM;
				goto fstype_failure;
			}
			strncpy(FTYPE,tok,len1);
			FTYPE[len1] = '\0';
#undef FTYPE 
			
#define OPT1 pvfstab_p->ptab_p[index].opt1 
			/* Extract the option 1 */
			tok = strtok(NULL,delims);
			len1 = strlen(tok);
			if (len1 < 0)
			{
				ret = -EINVAL;
				goto opt1_failure;
			}
			OPT1 = (PVFS_string)malloc(len1 + 1);
			if (!OPT1)
			{
				ret = -ENOMEM;
				goto opt1_failure;
			}
			strncpy(OPT1,tok,len1);
			OPT1[len1] = '\0';
#undef OPT1 
	
#define OPT2 pvfstab_p->ptab_p[index].opt2 
			/* Extract the option 2 */
			tok = strtok(NULL,delims);
			len1 = strlen(tok);
			if (len1 < 0)
			{
				ret = -EINVAL;
				goto opt2_failure;
			}
			OPT2 = (PVFS_string)malloc(len1 + 1);
			if (!OPT2)
			{
				ret = -ENOMEM;
				goto opt2_failure;
			}
			strncpy(OPT2,tok,len1);
			OPT2[len1] = '\0';
#undef OPT2 
		/* Increment the counter */
		index++;

		}
	}
	
	/* Close the pvfstab file */
	fclose(tab);

	return(0);

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
