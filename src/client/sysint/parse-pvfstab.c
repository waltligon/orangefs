/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <pint-sysint.h>

/* Function Prototypes */
static int mntlist_new(int num_mnts,pvfs_mntlist *mntlist_ptr);
/*static void free_pvfstab_entry(void *e_p);
static void dump_pvfstab_entry(void *e_p);
static int pvfstab_entry_dir_cmp(void *key, void *e_p);
pvfs_mntent *search_pvfstab(char *dir);
*/
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

	return(0);
}

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
	if (mnts->ptab_p)
		free(mnts->ptab_p);
	return;
}

/*static void dump_fstab_entry(void *e_p)
{
	struct mntent *ent = (struct mntent *)e_p;
	printf("fsname: %s, dir: %s, opts: %s\n", ent->mnt_fsname,
		ent->mnt_dir, ent->mnt_opts);
	return;
}
*/
/* fstab_entry_dir_cmp() - find a matching entry for a given file name
 *
 * ASSUMPTIONS:
 * - mnt_dir entries never have trailing slashes unless root is
 *   specified (which should never be)
 *
 * Returns 0 on match, non-zero if no match.
 */
/*
static int fstab_entry_dir_cmp(void *key, void *e_p)
{
	int sz, cmp;
	struct mntent *me_p = (struct mntent *)e_p;

	printf("cmp: %s, %s\n", (char *)key, me_p->mnt_dir);

	// get length of directory (not including terminator) 
	sz = strlen(me_p->mnt_dir);

	// compare dir to first part of key, drop out if no match 
	cmp = strncmp((char *)key, me_p->mnt_dir, sz);
	if (cmp) 
		return(cmp);

	// make sure that next character in key is a / or \0 
	key = (char *) key + sz;
	if (*(char *)key == '/' || *(char *)key == '\0') 
		return(0);
	return(-1);
}

struct mntent *search_fstab(char *dir)
{
	char* path_to_pvfstab = NULL;

	if (!pvfstab_p){
		if((path_to_pvfstab = getenv(PVFSTAB_ENV)) == NULL){
			parse_fstab(PVFSTAB_PATH);
		}
		else{
			parse_fstab(path_to_pvfstab);
		}
	}
	return((struct mntent *) llist_search(pvfstab_p, (void *) dir,
		fstab_entry_dir_cmp));
}*/
