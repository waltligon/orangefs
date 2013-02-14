#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <Windows.h>
#endif

#define CREATING_DLL	/* will export the functions into the DLL to create */

#include "orangefs-client.h" 
#include "pint-util.h"
#include "cred.h"
#include "str-utils.h"
#include "pvfs2.h"
#include "gossip.h"


/* GLOBALS */
OrangeFS_mntent **mntents = { 0 };
const PVFS_util_tab *tab;
BOOL MVS_DEBUGGING = FALSE;	/* will be set to true in orangefs_enable_debug */


void orangefs_enable_debug(int debugType, const char *filePath, int64_t gossip_mask)
{
	switch (debugType)
	{
	case OrangeFS_DEBUG_SYSLOG :
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_syslog(0);	/* current un-implemented on windows */
		break;
	case OrangeFS_DEBUG_STDERR :
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_stderr();
		break;
	case OrangeFS_DEBUG_FILE:
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_file(filePath, "a");
		break;
	case OrangeFS_DEBUG_MVS:
		gossip_set_debug_mask(1, gossip_mask);
		MVS_DEBUGGING = TRUE;
		/* dont need to enable anything to do this */
		break;
	case OrangeFS_DEBUG_SYSLOG | OrangeFS_DEBUG_STDERR:
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_syslog(0);
		gossip_enable_stderr();
		break;
	case OrangeFS_DEBUG_SYSLOG | OrangeFS_DEBUG_FILE:
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_syslog(0);
		gossip_enable_file(filePath, "a");
		break;
	case OrangeFS_DEBUG_SYSLOG | OrangeFS_DEBUG_MVS:
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_syslog(0);
		MVS_DEBUGGING = TRUE;
		break;
	case OrangeFS_DEBUG_STDERR | OrangeFS_DEBUG_FILE:
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_stderr();
		gossip_enable_file(filePath, "a");
		break;
	case OrangeFS_DEBUG_STDERR | OrangeFS_DEBUG_MVS:
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_stderr();
		MVS_DEBUGGING = TRUE;
		break;
	case OrangeFS_DEBUG_FILE | OrangeFS_DEBUG_MVS:
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_file(filePath, "a");
		MVS_DEBUGGING = TRUE;
		break;
	case OrangeFS_DEBUG_SYSLOG | OrangeFS_DEBUG_STDERR | OrangeFS_DEBUG_FILE :
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_syslog(0);
		gossip_enable_stderr();
		gossip_enable_file(filePath, "a");
		break;
	case OrangeFS_DEBUG_SYSLOG | OrangeFS_DEBUG_STDERR | OrangeFS_DEBUG_MVS :
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_syslog(0);
		gossip_enable_stderr();
		MVS_DEBUGGING = TRUE;
		break;
	case OrangeFS_DEBUG_SYSLOG | OrangeFS_DEBUG_FILE | OrangeFS_DEBUG_MVS :
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_syslog(0);
		gossip_enable_file(filePath, "a");
		MVS_DEBUGGING = TRUE;
		break;
	case OrangeFS_DEBUG_STDERR | OrangeFS_DEBUG_FILE | OrangeFS_DEBUG_MVS :
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_stderr();
		gossip_enable_file(filePath, "a");
		MVS_DEBUGGING = TRUE;
		break;
	case OrangeFS_DEBUG_ALL:
		gossip_set_debug_mask(1, gossip_mask);
		gossip_enable_syslog(0);
		gossip_enable_stderr();
		gossip_enable_file(filePath, "a");
		MVS_DEBUGGING = TRUE;
		break;
	default :
		break;
	}
}

void orangefs_debug_print(const char *format, ...)
{
#ifdef ORANGEFS_DEBUG
	char buff[GOSSIP_BUF_SIZE];
	va_list argp;

#ifdef WIN32
	if (MVS_DEBUGGING)
		OutputDebugString("---- DEBUG ----\n");
#endif
	
	va_start(argp, format);
	vsprintf(buff, format, argp);
	va_end(argp);

	gossip_debug(OrangeFS_WIN_CLIENT_DEBUG, buff, argp);	

#ifdef WIN32
	if (MVS_DEBUGGING)
		OutputDebugString(buff);
#endif
#endif
}

/* caller needs to allocate the space and zero out the **mntents parameter */
int orangefs_load_tabfile(const char *path, 
						  OrangeFS_mntent **mntents, 
						  char *error_msg, 
						  size_t error_msg_len)
{
	/* currently unimplemented */
}

int orangefs_initialize(OrangeFS_fsid *fsid,		/* not currently implemented */
						OrangeFS_credential *cred,	/* not currently implemented */
						OrangeFS_mntent *mntent, 
						char *error_msg, 
						size_t error_msg_len, 
						const char *tabF,
						int debugType,
						const char *debugFile)
{
	char exe_path[MAX_PATH];
	char *tabfile, *p;
	int ret, malloc_flag, i, found_one = 0;

	ret = GetModuleFileName(NULL, exe_path, MAX_PATH);
	if (ret)
	{
		tabfile = (char *)malloc(MAX_PATH);
		malloc_flag = TRUE;

		/* cut off the exe file, just get up to last directory */
		p = strrchr(exe_path, '\\');
		if (p)
			*p = '\0';

		strcpy(tabfile, exe_path);
		strcat(tabfile, tabF);	/* now we have absolute path to orangefstab file */
	}

	if (tabfile)
	{
		orangefs_debug_print("Using tabfile: %s\n", tabfile);
		do {
			ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
			if (ret != 0)
			{
				orangefs_debug_print("Failed to initialize the file system with call to: PVFS_sys_initialize()\n");
				free(tabfile);
				return -1;
			}

			/* read tab file */
			tab = PVFS_util_parse_pvfstab(tabfile);
			if (!tab)
			{
				orangefs_debug_print("Failed to parse tabfile: %s", tabfile);
				free(tabfile);
				return -1;
			}

			/* initialize file system */
			ret = PVFS_sys_fs_add(&tab->mntent_array[0]);	/* currently only supports adding 1 mount entry at a time with a call to orangefs_initialize() */
			if (ret == 0)
				found_one = 1;
			
			/* now copy the mntent data into the mntnet passed in */
			memcpy(mntent, &tab->mntent_array[0], sizeof(OrangeFS_mntent));

			if (!found_one)
			{
				orangefs_debug_print("orangefs_initialize: could not initialize any file systems from %s\n", tab->tabfile_name);
				PINT_release_pvfstab();
				PVFS_sys_finalize();
				ret = -1;
			}

			if (ret < 0)
			{
				orangefs_debug_print("ERROR initializing OrangeFS file system...\nTrying again in 30 seconds...\n");
				Sleep(30000); /* 30 seconds */
			}
		}while (ret < 0);
	}

	if (malloc_flag)
		free(tabfile);

	/* is there a gossip cleanup or turn off function? */

    return 0;
}

OrangeFS_mntent *get_mntent(OrangeFS_fsid *fs_id)
{
	int count = 0;
	while ((*mntents))
	{
		if ((*mntents)[count].fs_id == *fs_id)
			return (&(*mntents)[count]);
		else
			++count;
	}
	return (OrangeFS_mntent *) NULL;
}

int orangefs_lookup(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *path, OrangeFS_handle *handle)
{
	int ret = 0;
	OrangeFS_mntent *mntent = get_mntent(fs_id);
	PVFS_sysresp_lookup resp;

	/* lookup the given path on the FS - do not follow links */
    ret = PVFS_sys_lookup(*fs_id, path, (PVFS_credential *) cred, &resp,
        PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);

    if (ret == 0)
        *handle = (OrangeFS_handle) resp.ref.handle;

    return ret;
}

/* split path into base dir and entry name components */
int split_path(char *fs_path, char *base_dir, size_t base_dir_len, char *entry_name, size_t entry_name_len)
{
    int ret;

    /* get base dir */
    ret = PINT_get_base_dir(fs_path, base_dir, base_dir_len);

    if (ret != 0)
        return ret;

    /* get entry name */
    ret = PINT_remove_base_dir(fs_path, entry_name, entry_name_len);

    return ret;
}

#define FS_MAX_LINKS    256

/* Workaround to follow PVFS2 file links */
int orangefs_lookup_follow_links(OrangeFS_fsid fs_id, OrangeFS_credential *cred, char *fs_path, OrangeFS_sysresp_lookup *resp, OrangeFS_attr *attr)
{
    char *real_path, *link_path;
    PVFS_sysresp_getattr resp_getattr;
    int ret, link_flag, count;

    if (fs_path == NULL || strlen(fs_path) == 0 || cred == NULL ||
        resp == NULL)
        return -1;

    /* copy to be used for link paths */
    real_path = strdup(fs_path);
    count = 0;
    do 
    {
        link_flag = FALSE;

        /* lookup the given path on the FS */
        ret = PVFS_sys_lookup(fs_id, real_path, (PVFS_credential *) cred, (PVFS_sysresp_lookup *) resp,
            PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
        if (ret != 0)
            break;

        /* check if it's a link */
        memset(&resp_getattr, 0, sizeof(resp_getattr));
        ret = PVFS_sys_getattr(((PVFS_sysresp_lookup *)resp)->ref, PVFS_ATTR_SYS_ALL_NOHINT, (PVFS_credential *) cred,
            &resp_getattr, NULL);
        if (ret != 0)
            break;

        if (resp_getattr.attr.link_target != NULL)
        {
            /* we have a link -- compute the target */
            link_flag = TRUE;
            link_path = (char *) malloc(ORANGEFS_PATH_MAX + 4);
            if (link_path == NULL)
            {
                PVFS_util_release_sys_attr(&resp_getattr.attr);
                ret = -1;
                break;
            }
            /* if link target is not absolute, prepend path */
            memset(link_path, 0, ORANGEFS_PATH_MAX + 4);
            if (resp_getattr.attr.link_target[0] != '/')
            {
                char *lastslash = strrchr(real_path, '/');
                if (lastslash)
                {
                    /* copy path including slash */
                    strncpy(link_path, real_path, lastslash - real_path + 1);
                    strncat(link_path, resp_getattr.attr.link_target, 
                        ORANGEFS_PATH_MAX - strlen(link_path));
                }
                else
                {
                    free(link_path);
                    PVFS_util_release_sys_attr(&resp_getattr.attr);
                    ret = -1;                    
                    break;
                }
            }
            else
            {
                /* use absolute link target */
                strncpy(link_path, resp_getattr.attr.link_target, ORANGEFS_PATH_MAX);
            }
            link_path[ORANGEFS_PATH_MAX-1] = '\0';
            /* get file name */
            free(real_path);
            real_path = (char *) malloc(ORANGEFS_PATH_MAX + 4);
            ret = PVFS_util_resolve(link_path, (PVFS_fs_id *)&fs_id, real_path, ORANGEFS_PATH_MAX);
            if (ret == -PVFS_ENOENT)
            {
                /* link may not include the mount point, e.g. /mnt/pvfs2.
                   in this case just use link_path */
                strncpy(real_path, link_path, ORANGEFS_PATH_MAX);
                ret = 0;
            }
            free(link_path);
            /* free attr buffer */            
            PVFS_util_release_sys_attr(&resp_getattr.attr);
        }
        else
        {
            /* copy the attr to the buffer */
            if (attr != NULL)
            {
                memcpy(attr, &resp_getattr.attr, sizeof(OrangeFS_attr));
            }
            else
            {
                PVFS_util_release_sys_attr(&resp_getattr.attr);
            }
        }                
    } while (ret == 0 && link_flag && (++count) < MAX_LINKS);

    free(real_path);

    if (ret == 0 && count >= FS_MAX_LINKS)
    {
        ret = -1;
    }

    return ret;
}

/* get the attributes of the target of a symlink */
int orangefs_get_symlink_attr(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *fs_path, char *target, OrangeFS_object_ref *ref, OrangeFS_attr *attr)
{
    int ret;
    char link_path[ORANGEFS_PATH_MAX + 4];
    PVFS_sysresp_lookup resp_lookup;

    if (fs_path == NULL || target == NULL || ref == NULL ||
        cred == NULL || attr == NULL)
    {
        return -1;
    }

    if (strlen(fs_path) == 0 || strlen(target) == 0)
    {
        return -1;
    }

    /* if target is relative, append to fs_path */
    memset(link_path, 0, ORANGEFS_PATH_MAX+4);
    if (target[0] != '/') 
    {
        strncpy(link_path, fs_path, ORANGEFS_PATH_MAX);        
        if (link_path[strlen(link_path)-1] != '/')
        {
            strcat(link_path, "/");
        }
        strncat(link_path, target, ORANGEFS_PATH_MAX - strlen(link_path));
    }
    else
    {
        OrangeFS_fsid fs_id;
        /* use PVFS_util_resolve to remove preceding mount point */
        ret = PVFS_util_resolve(target, (PVFS_fs_id *) &fs_id, link_path, ORANGEFS_PATH_MAX);
        if (ret == -PVFS_ENOENT)
        {
            /* just use target as path */
            strncpy(link_path, target, ORANGEFS_PATH_MAX);
        }
        else if (ret != 0)
        {
            return ret;
        }
    }

    /* lookup the target and retrieve the attributes */
    ret = orangefs_lookup_follow_links(*fs_id, cred, link_path, (OrangeFS_sysresp_lookup *) &resp_lookup, attr);

    return ret;
}

int orangefs_create(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *path, unsigned perms, OrangeFS_handle *handle)	/* maybe pass in a string parameter for setting output errors */
{
	size_t len;
	char *base_dir, *entry_name;
	int ret = 0;
	PVFS_object_ref parent_ref;
	PVFS_sysresp_lookup resp_lookup;
	PVFS_sys_attr attr;
	PVFS_sysresp_create resp_create;

	if (fs_id == 0 || cred == NULL || path == NULL)
		return -1;

	/* cannot be only a dir */
    if (path[strlen(path)-1] == '/')
        return -1;

	/* split path into path and file components */
    len = strlen(path) + 1;
    base_dir = (char *) malloc(len);    
    entry_name = (char *) malloc(len);
    if (base_dir == NULL || entry_name == NULL)
        return -1;

	ret = split_path(path, base_dir, len, entry_name, len);
    if (ret != 0)
        goto create_exit;

	/* lookup parent path - follow links */
    ret = orangefs_lookup_follow_links(*fs_id, cred, base_dir, (OrangeFS_sysresp_lookup *) &resp_lookup, NULL);
    if (ret != 0)
        goto create_exit;

	parent_ref.fs_id = resp_lookup.ref.fs_id;	/* NOT SURE IF WE WANT THIS OR THE FS_ID PASSED IN */
    parent_ref.handle = resp_lookup.ref.handle;

	/* create file */
    memset(&attr, 0, sizeof(PVFS_sys_attr));
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = cred->userid;
    attr.group = cred->group_array[0];
    /* configurable in options */
    attr.perms = perms;
    attr.atime = attr.mtime = attr.ctime = time(NULL);

    ret = PVFS_sys_create(entry_name, parent_ref, attr,
              (PVFS_credential *) cred, NULL, &resp_create, NULL, NULL);
    if (ret)
        goto create_exit;

    *handle = (OrangeFS_handle) resp_create.ref.handle;

create_exit:
	free(base_dir);
	free(entry_name);

	return ret;
}

int orangefs_create_h(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, OrangeFS_handle parent_handle, char *name, unsigned perms, OrangeFS_handle *handle)
{
	int ret = 0;
	PVFS_object_ref parent_ref;
	PVFS_sys_attr attr;
	PVFS_sysresp_create resp_create;

	if (fs_id == 0 || cred == NULL || name == NULL)
		return -1;

	/* cannot be only a dir */
    if (name[strlen(name)-1] == '/')
        return -1;

	parent_ref.fs_id = (PVFS_fs_id) fs_id;
    parent_ref.handle = (PVFS_handle) parent_handle;

	/* create file */
    memset(&attr, 0, sizeof(OrangeFS_attr));
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = cred->userid;
    attr.group = cred->group_array[0];
    /* configurable in options */
    attr.perms = perms;
    attr.atime = attr.mtime = attr.ctime = time(NULL);

	ret = PVFS_sys_create(name, parent_ref, attr, (PVFS_credential *) cred, NULL, &resp_create, NULL, NULL);

	if (ret)
		return -1;

    *handle = (OrangeFS_handle) resp_create.ref.handle;

	return ret;
}

int orangefs_remove(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *path)
{
	size_t len;
	char *base_dir, *entry_name;
	int ret = 0;
	PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref parent_ref;

	if (fs_id == 0 || cred == NULL || strlen(path) == 0 || path == NULL)
		return -1;

	/* split path into path and file components */
    len = strlen(path) + 1;
    base_dir = (char *) malloc(len);    
    entry_name = (char *) malloc(len);
    if (base_dir == NULL || entry_name == NULL)
    {
        return -PVFS_ENOMEM;
    }
    ret = split_path(path, base_dir, len, entry_name, len);
    if (ret != 0)
        goto remove_exit;

	/* lookup parent entry - follow links */
    ret = orangefs_lookup_follow_links(*fs_id, cred, base_dir, (OrangeFS_sysresp_lookup *) &resp_lookup, NULL);
    if (ret != 0)
        goto remove_exit;

	parent_ref.fs_id = (PVFS_fs_id) fs_id;
    parent_ref.handle = resp_lookup.ref.handle;

    ret = PVFS_sys_remove(entry_name, parent_ref, (PVFS_credential *) cred, NULL);

remove_exit:
	free(base_dir);
	free(entry_name);

	return ret;
}

int orangefs_remove_h(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, OrangeFS_handle handle, char *name)	
{
	int ret = 0;
    PVFS_object_ref parent_ref;

	if (fs_id == 0 || cred == NULL)
		return -1;

	parent_ref.handle = (PVFS_handle) handle;
	parent_ref.fs_id = (PVFS_fs_id) fs_id;

    ret = PVFS_sys_remove(name, parent_ref, (PVFS_credential *) cred, NULL);

	return ret;
}

int orangefs_rename(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *old_path, char *new_path)
{
	size_t len;
	char *old_base_dir, *old_entry_name,
		 *new_base_dir, *new_entry_name;
	PVFS_sysresp_lookup old_resp_lookup, new_resp_lookup;
    PVFS_object_ref old_parent_ref, new_parent_ref;
	int ret = 0;

	if (fs_id == 0 || cred == NULL || old_path == NULL || new_path == NULL || strlen(old_path) == 0 || strlen(new_path) == 0)
		return -1;

	/* split paths into path and file components */
    len = strlen(old_path) + 1;
    old_base_dir = (char *) malloc(len);    
    old_entry_name = (char *) malloc(len);
    if (old_base_dir == NULL || old_entry_name == NULL)
    {
        return -1;
    }
    ret = split_path(old_path, old_base_dir, len, old_entry_name, len);
    if (ret != 0)
        goto rename_exit;

	old_parent_ref.fs_id = old_resp_lookup.ref.fs_id;
    old_parent_ref.handle = old_resp_lookup.ref.handle;

    len = strlen(new_path) + 1;
    new_base_dir = (char *) malloc(len);    
    new_entry_name = (char *) malloc(len);
    if (new_base_dir == NULL || new_entry_name == NULL)
    {
        return -1;
    }
    ret = split_path(new_path, new_base_dir, len, new_entry_name, len);
    if (ret != 0)
        goto rename_exit;

	new_parent_ref.fs_id = new_resp_lookup.ref.fs_id;
    new_parent_ref.handle = new_resp_lookup.ref.handle;

    /* rename/move the file */
    ret = PVFS_sys_rename(old_entry_name, old_parent_ref, new_entry_name,
                          new_parent_ref, (PVFS_credential *) cred, NULL);

rename_exit:
	free(old_entry_name);
	free(old_base_dir);
	free(new_entry_name);
	free(new_base_dir);

	return ret;
}

int orangefs_getattr(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *path, OrangeFS_attr *attr)
{
    int ret;
    PVFS_sysresp_lookup resp_lookup;

    if (path == NULL || strlen(path) == 0 ||
        attr == NULL || cred == NULL)
        return -1;

    /* lookup file - follow links 
       attributes will be read and placed in attr */
    ret = orangefs_lookup_follow_links(*fs_id, cred, path, (OrangeFS_sysresp_lookup *) &resp_lookup, attr);
    if (ret != 0)
        goto fs_getattr_exit;		

    /* free attr bufs */
    PVFS_util_release_sys_attr((PVFS_sys_attr *) attr);
   
fs_getattr_exit:

    return ret;
}

int orangefs_setattr(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *fs_path, OrangeFS_attr *attr)
{
    int ret;
    PVFS_sysresp_lookup resp_lookup;
	PVFS_sys_attr pvfsAttr;

	/* to workout the compiler warnings when you can't cast non-pointer values even though they're the same type according to their binary */
	memcpy(&pvfsAttr, attr, sizeof(OrangeFS_attr)); 

    if (fs_path == NULL || strlen(fs_path) == 0 || cred == NULL)
        return -1;

    /* lookup file - follow links */
    ret = orangefs_lookup_follow_links(*fs_id, cred, fs_path, (OrangeFS_sysresp_lookup *) &resp_lookup, NULL);
    if (ret != 0)
        goto setattr_exit;

    /* set attributes */
    ret = PVFS_sys_setattr(resp_lookup.ref, pvfsAttr, (PVFS_credential *) cred, NULL);

setattr_exit:

    return ret;
}

int orangefs_mkdir(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *fs_path, OrangeFS_handle *handle, unsigned perms)
{
    char *base_dir, *entry_name;
    size_t len;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref parent_ref;
	PVFS_sys_attr attr;
    PVFS_sysresp_mkdir resp_mkdir;
    int ret;

    if (fs_path == NULL || strlen(fs_path) == 0 || cred == NULL)
        return -1;
    
    /* split path into path and file components */
    len = strlen(fs_path) + 1;
    base_dir = (char *) malloc(len);    
    entry_name = (char *) malloc(len);
    if (base_dir == NULL || entry_name == NULL)
    {
        return -1;
    }
    ret = split_path(fs_path, base_dir, len, entry_name, len);
    if (ret != 0)
        goto mkdir_exit;

    /* lookup parent path - follow links */
    ret = orangefs_lookup_follow_links(*fs_id, cred, base_dir, (OrangeFS_sysresp_lookup *) &resp_lookup, NULL);

    if (ret != 0)
        goto mkdir_exit;

    parent_ref.fs_id = resp_lookup.ref.fs_id;	/* do we want this or the fs_id passed in to the function? */
    parent_ref.handle = resp_lookup.ref.handle;

    /* create file */
    memset(&attr, 0, sizeof(OrangeFS_attr));
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = cred->userid;
    attr.group = cred->group_array[0];
    /* configurable in options */
    attr.perms = perms;
    attr.atime = attr.mtime = attr.ctime = time(NULL);

    ret = PVFS_sys_mkdir(entry_name, parent_ref, attr, (PVFS_credential *) cred,
                         &resp_mkdir, NULL);

    if (ret == 0)
        *handle = (OrangeFS_handle) resp_mkdir.ref.handle;

mkdir_exit:
    free(entry_name);
    free(base_dir);

    return ret;
}

int orangefs_io(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, enum ORANGEFS_io_type io_type, char *fs_path, void *buffer, size_t buffer_len, uint64_t offset, OrangeFS_size *op_len)
{
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref object_ref;
    PVFS_Request file_req, mem_req;
    PVFS_sysresp_io resp_io;
    int ret;

    if (fs_path == NULL || strlen(fs_path) == 0 ||
        buffer == NULL || cred == NULL)
        return -1;

    /* lookup file - follow links */
    ret = orangefs_lookup_follow_links(*fs_id, cred, fs_path, (OrangeFS_sysresp_lookup *) &resp_lookup, NULL);
    if (ret != 0)
        goto io_exit;

    /* copy object ref */
    object_ref.fs_id = resp_lookup.ref.fs_id;
    object_ref.handle = resp_lookup.ref.handle;

    /* get memory buffer */
    file_req = PVFS_BYTE;

    ret = PVFS_Request_contiguous(buffer_len, PVFS_BYTE, &(mem_req));
    if (ret != 0)
        goto io_exit;

    /* perform io operation */
    ret = PVFS_sys_io(object_ref, file_req, offset, buffer, mem_req, (PVFS_credential *) cred, &resp_io, (enum PVFS_io_type) io_type, NULL);
    if (ret == 0 && op_len != NULL)
    {
        *op_len = resp_io.total_completed;
    }

    PVFS_Request_free(&mem_req);

io_exit:

    return ret;
}

int orangefs_flush(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *fs_path)
{
    int ret;
    PVFS_sysresp_lookup resp_lookup;

    if (fs_path == NULL || strlen(fs_path) == 0 || cred == NULL)
        return -1;

    /* lookup file - follow links */
    ret = orangefs_lookup_follow_links(*fs_id, cred, fs_path, (OrangeFS_sysresp_lookup *) &resp_lookup, NULL);
    if (ret != 0)
        goto flush_exit;

    /* flush file */
    ret = PVFS_sys_flush(resp_lookup.ref, (PVFS_credential *) cred, NULL);

flush_exit:

    return ret;
}

int orangefs_find_files(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *fs_path, OrangeFS_ds_position *token, int32_t incount, int32_t *outcount, char **filename_array, OrangeFS_attr *attr_array)
{
    int ret, i;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_readdirplus resp_readdirplus;
    PVFS_object_ref ref;
    PVFS_sysresp_getattr resp_getattr;

    if (fs_path == NULL || strlen(fs_path) == 0 || token == NULL ||
        outcount == NULL || filename_array == NULL || attr_array == NULL || cred == NULL)
        return -1;

    /* lookup path - follow links */
    ret = orangefs_lookup_follow_links(*fs_id, cred, fs_path, (OrangeFS_sysresp_lookup *) &resp_lookup, NULL);

    if (ret != 0)
        goto readdir_exit;

    /* read up to incount entries, starting with token */
    memset(&resp_readdirplus, 0, sizeof(resp_readdirplus));
    ret = PVFS_sys_readdirplus(resp_lookup.ref, *token, incount, (PVFS_credential *) cred, 
                               PVFS_ATTR_SYS_ALL_NOHINT, &resp_readdirplus, NULL);
    if (ret != 0)
        goto readdir_exit;

    /* copy output results */
    *outcount = resp_readdirplus.pvfs_dirent_outcount;
    if (*outcount != 0)
    {
        *token = resp_readdirplus.token;
            
        for (i = 0; i < *outcount; i++)
        {
            strncpy(filename_array[i], resp_readdirplus.dirent_array[i].d_name, ORANGEFS_NAME_MAX);
            if (resp_readdirplus.stat_err_array[i] == 0)
            {
                memcpy(&attr_array[i], &resp_readdirplus.attr_array[i], sizeof(PVFS_sys_attr));
                /* TODO: DEBUG */
                if (resp_readdirplus.attr_array[i].link_target)
                {
                    // orangefs_debug_print("    %s link: %s\n", filename_array[i], resp_readdirplus.attr_array[i].link_target);                    
                }
            }
            else
            {
                /* attempt to get attrs with PVFS_sys_getattr */
                ref.fs_id = *fs_id;
                ref.handle = resp_readdirplus.dirent_array[i].handle;
                memset(&resp_getattr, 0, sizeof(PVFS_sysresp_getattr));
                ret = PVFS_sys_getattr(ref, PVFS_ATTR_SYS_ALL_NOHINT, (PVFS_credential *) cred,
                    &resp_getattr, NULL);
                if (ret == 0)
                {
                    memcpy(&attr_array[i], &resp_getattr.attr, sizeof(OrangeFS_attr));  
                }
                else {
                    break;
                }
            }
            /* get attributes of symbolic link if applicable */
            if (attr_array[i].link_target != NULL)
            {
                ref.fs_id = *fs_id;
                ref.handle = resp_readdirplus.dirent_array[i].handle;
                /* note: ignore return code... just use attrs of the symlink */
                orangefs_get_symlink_attr(fs_id, cred, fs_path, attr_array[i].link_target, (OrangeFS_object_ref *) &ref, &attr_array[i]);
            }

            /* clear allocated fields */
            PVFS_util_release_sys_attr((PVFS_sys_attr *) &attr_array[i]);
        }

        /* free memory */        
        free(resp_readdirplus.dirent_array);
        free(resp_readdirplus.stat_err_array);
        free(resp_readdirplus.attr_array);
    }

readdir_exit:

    return ret;
}

int orangefs_get_diskfreespace(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, OrangeFS_size *free_bytes, OrangeFS_size *total_bytes)
{
    PVFS_sysresp_statfs resp_statfs;
    int ret;

    if (free_bytes == NULL || total_bytes == NULL || cred == NULL)
    {
        return -1;
    }

    ret = PVFS_sys_statfs(*fs_id, (PVFS_credential *) cred, &resp_statfs, NULL);

    if (ret == 0)
    {
        *free_bytes = resp_statfs.statfs_buf.bytes_available;
        *total_bytes = resp_statfs.statfs_buf.bytes_total;
    }

    return ret;
}

int orangefs_finalize()
{
    /* TODO */
    PVFS_sys_finalize();

    return 0;
}

int orangefs_credential_init(OrangeFS_credential *cred)
{
	if (cred == NULL)
		return -1;

	memset(cred, 0, sizeof(OrangeFS_credential));

	/* blank issuer */
    cred->issuer = strdup("");
    if (!cred->issuer)
    {
        return -1;
    }

	return 0;
}

int orangefs_credential_set_user(OrangeFS_credential *cred, OrangeFS_uid uid)
{
	if (cred == NULL)
		return -1;

	cred->userid = uid;

	return 0;
}

void orangefs_credential_add_group(OrangeFS_credential *cred, OrangeFS_gid gid)
{
	OrangeFS_gid *group_array;
    unsigned int i;

    if (cred->num_groups > 0)
    {
        /* copy existing group array and append group */
        cred->num_groups++;
        group_array = (OrangeFS_gid *) malloc(sizeof(OrangeFS_gid) * cred->num_groups);
        for (i = 0; i < cred->num_groups - 1; i++)
        {
            group_array[i] = cred->group_array[i];
        }
        group_array[cred->num_groups-1] = gid;

        free(cred->group_array);
        cred->group_array = group_array;
    }
    else
    {
        /* add one group */
        cred->group_array = (OrangeFS_gid *) malloc(sizeof(OrangeFS_gid));
        cred->group_array[0] = gid;
        cred->num_groups = 1;
    }
}

void orangefs_cleanup_credentials(OrangeFS_credential *cred)
{
	if (cred) 
    {
        if (cred->group_array)
        {
            free(cred->group_array);
            cred->group_array = NULL;
        }
        if (cred->signature)
        {
            free(cred->signature);
            cred->signature = NULL;
        }
        if (cred->issuer)
        {
            free(cred->issuer);
            cred->issuer = NULL;
        }

        cred->num_groups = 0;
        cred->sig_size = 0;
    }
}

/* check if credential is a member of group */
int orangefs_credential_in_group(OrangeFS_credential *cred, OrangeFS_gid group)
{
    unsigned int i;

    for (i = 0; i < cred->num_groups; i++)
    {
        if (cred->group_array[i] == group)
        {
            return 1;
        }
    }

    return 0;
}

/* set the credential timeout to current time + timeout
   use ORANGEFS_DEFAULT_CREDENTIAL_TIMEOUT */
void orangefs_credential_set_timeout(OrangeFS_credential *cred, OrangeFS_time timeout)
{
    cred->timeout = PINT_util_get_current_time() + timeout;
}

