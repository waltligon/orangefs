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
		/* Is the line blank? */
		if (strlen(line) > 1)
		{
			/* Skip the first token */
			tok = strtok(line,delims);

			/* Extract the bmi address */
			tok = strtok(NULL,delims);
#define META_ADDR pvfstab_p->ptab_p[index].meta_addr 
			root_mnt = strrchr(tok,'/');
			META_ADDR = (PVFS_string)malloc(strlen(tok)-strlen(root_mnt) + 1);
			if (!META_ADDR)
			{
				ret = -ENOMEM;
				goto metaaddr_failure;
			}
			strncpy(META_ADDR,tok,strlen(tok)-strlen(root_mnt));
			META_ADDR[strlen(tok) - strlen(root_mnt)] = '\0';
#undef META_ADDR 

#define SERV_MNT pvfstab_p->ptab_p[index].serv_mnt_dir 
			/* Extract the Root Mount Point */
			SERV_MNT = (PVFS_string)malloc(strlen(root_mnt) + 1);
			if (!SERV_MNT)
			{
				ret = -ENOMEM;
				goto servmnt_failure;
			}
			strncpy(SERV_MNT,root_mnt,strlen(root_mnt));
			SERV_MNT[(strlen(root_mnt))] = '\0';
#undef SERV_MNT 

#define LOCAL_MNT pvfstab_p->ptab_p[index].local_mnt_dir 
			/* Extract the Local Mount Point */
			tok = strtok(NULL,delims);
			LOCAL_MNT = (PVFS_string)malloc(strlen(tok) + 1);
			if (!LOCAL_MNT)
			{
				ret = -ENOMEM;
				goto localmnt_failure;
			}
			strncpy(LOCAL_MNT,tok,strlen(tok));
			LOCAL_MNT[(strlen(tok))] = '\0';
#undef LOCAL_MNT 

#define FTYPE pvfstab_p->ptab_p[index].fs_type 
			/* Extract the File System Type */
			tok = strtok(NULL,delims);
			FTYPE = (PVFS_string)malloc(strlen(tok) + 1);
			if (!FTYPE)
			{
				ret = -ENOMEM;
				goto fstype_failure;
			}
			strncpy(FTYPE,tok,strlen(tok));
			FTYPE[(strlen(tok))] = '\0';
#undef FTYPE 
			
#define OPT1 pvfstab_p->ptab_p[index].opt1 
			/* Extract the option 1 */
			tok = strtok(NULL,delims);
			OPT1 = (PVFS_string)malloc(strlen(tok) + 1);
			if (!OPT1)
			{
				ret = -ENOMEM;
				goto opt1_failure;
			}
			strncpy(OPT1,tok,strlen(tok));
			OPT1[(strlen(tok))] = '\0';
#undef OPT1 
	
#define OPT2 pvfstab_p->ptab_p[index].opt2 
			/* Extract the option 2 */
			tok = strtok(NULL,delims);
			OPT2 = (PVFS_string)malloc(strlen(tok) + 1);
			if (!OPT2)
			{
				ret = -ENOMEM;
				goto opt2_failure;
			}
			strncpy(OPT2,tok,strlen(tok));
			OPT2[(strlen(tok))] = '\0';
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
		if (mnts->ptab_p[i].serv_mnt_dir)
			free(mnts->ptab_p[i].serv_mnt_dir);
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
			smnt_len = strlen(mnt.ptab_p[i].serv_mnt_dir);
			mntent->serv_mnt_dir = (char *)malloc(smnt_len + 1);
			if (!mntent->serv_mnt_dir)
			{
				ret = -ENOMEM;
				goto unlock_exit;
			}
			strncpy(mntent->serv_mnt_dir,mnt.ptab_p[i].serv_mnt_dir,smnt_len);
			mntent->serv_mnt_dir[smnt_len] = '\0';
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
