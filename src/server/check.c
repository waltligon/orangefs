/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Changes by Acxiom Corporation to add PINT_check_mode() helper function
 * as a replacement for check_mode() in permission checking, also added
 * PINT_check_group() for supplimental group support 
 * Copyright © Acxiom Corporation, 2005.
 *
 * See COPYING in top-level directory.
 */

/*
 * Server-specific utility functions, to check modes and ACLs.
 */
#include <string.h>
#include <assert.h>
#include <pwd.h>
#include <grp.h>

#include "pvfs2-debug.h"
#include "gen-locks.h"
#include "gossip.h"
#include "bmi-byteswap.h"
#include "check.h"

static gen_mutex_t check_group_mutex = GEN_MUTEX_INITIALIZER;
static int pw_buf_size = 1024;     /* 1K */
static int max_usr_groups = 65536; /* 64K */
static char* check_group_pw_buffer = NULL;
static gid_t* usr_groups = NULL;
static int PINT_check_group(uid_t uid, gid_t gid);

/* PINT_check_mode()
 *
 * checks to see if the type of access described by "access_type" is permitted 
 * for user "uid" of group "gid" on the object with attributes "attr"
 *
 * returns 0 on success, -PVFS_EACCES if permission is not granted
 */
int PINT_check_mode(
    PVFS_object_attr *attr,
    PVFS_uid uid, PVFS_gid gid,
    enum PINT_access_type access_type)
{
    int in_group_flag = 0;
    int ret = 0;

    /* if we don't have masks for the permission information that we
     * need, then the system is broken
     */
    assert(attr->mask & PVFS_ATTR_COMMON_UID &&
           attr->mask & PVFS_ATTR_COMMON_GID &&
           attr->mask & PVFS_ATTR_COMMON_PERM);

    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - check_mode called --- "
                 "(uid=%d,gid=%d,access_type=%d)\n", uid, gid, access_type);
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - object attributes --- "
                 "(uid=%d,gid=%d,mode=%d)\n", attr->owner, attr->group,
                 attr->perms);

    /* give root permission, no matter what */
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG,
                 " - checking if uid (%d) is root ...\n", uid);
    if (uid == 0)
    {
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - yes\n");
        return 0;
    }
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - no\n");

    /* see if uid matches object owner */
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - checking if owner (%d) "
        "matches uid (%d)...\n", attr->owner, uid);
    if(attr->owner == uid)
    {
        /* see if object user permissions match access type */
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - yes\n");
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - checking if permissions "
            "(%d) allows access type (%d) for user...\n", attr->perms, access_type);
        if(access_type == PINT_ACCESS_READABLE && (attr->perms &
            PVFS_U_READ))
        {
            gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - yes\n");
            return(0);
        }
        if(access_type == PINT_ACCESS_WRITABLE && (attr->perms &
            PVFS_U_WRITE))
        {
            gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - yes\n");
            return(0);
        }
        if(access_type == PINT_ACCESS_EXECUTABLE && (attr->perms &
            PVFS_U_EXECUTE))
        {
            gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - yes\n");
            return(0);
        }
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - no\n");
    }
    else
    {
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - no\n");
    }

    /* see if other bits allow access */
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - checking if permissions "
        "(%d) allows access type (%d) by others...\n", attr->perms, access_type);
    if(access_type == PINT_ACCESS_READABLE && (attr->perms &
        PVFS_O_READ))
    {
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - yes\n");
        return(0);
    }
    if(access_type == PINT_ACCESS_WRITABLE && (attr->perms &
        PVFS_O_WRITE))
    {
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - yes\n");
        return(0);
    }
    if(access_type == PINT_ACCESS_EXECUTABLE && (attr->perms &
        PVFS_O_EXECUTE))
    {
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - yes\n");
        return(0);
    }
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - no\n");

    /* see if gid matches object group */
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - checking if group (%d) "
        "matches gid (%d)...\n", attr->group, gid);
    if(attr->group == gid)
    {
        /* default group match */
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - yes\n");
        in_group_flag = 1;
    }
    else
    {
        /* no default group match, check supplementary groups */
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - no\n");
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - checking for"
            " supplementary group match...\n");
        ret = PINT_check_group(uid, attr->group);
        if(ret == 0)
        {
            gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - yes\n");
            in_group_flag = 1;
        }
        else
        {
            gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - no\n");
            if(ret != -PVFS_ENOENT)
            {
                /* system error; not just failed match */
                return(ret);
            }
        }
    }

    if(in_group_flag)
    {
        /* see if object group permissions match access type */
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - checking if permissions "
            "(%d) allows access type (%d) for group...\n", attr->perms, access_type);
        if(access_type == PINT_ACCESS_READABLE && (attr->perms &
            PVFS_G_READ))
        {
            gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - yes\n");
            return(0);
        }
        if(access_type == PINT_ACCESS_WRITABLE && (attr->perms &
            PVFS_G_WRITE))
        {
            gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - yes\n");
            return(0);
        }
        if(access_type == PINT_ACCESS_EXECUTABLE && (attr->perms &
            PVFS_G_EXECUTE))
        {
            gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - yes\n");
            return(0);
        }
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, " - no\n");
    }
  
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "******PINT_check_mode: denying access\n");
    /* default case: access denied */
    return -PVFS_EACCES;
}

/* PINT_check_group()
 *
 * checks to see if uid is a member of gid
 * 
 * returns 0 on success, -PVFS_ENOENT if not a member, other PVFS error codes
 * on system failure
 */
static int PINT_check_group(uid_t uid, gid_t gid)
{
    struct passwd pwd;
    struct passwd* pwd_p = NULL;
    int i = 0;
    int ret = -1;
    int num_usr_groups = max_usr_groups;

    /* Explanation: 
     *
     * We use the _r variants of getpwuid and getgrgid in order to insure
     * thread safety; particularly if this function ever gets called in a
     * client side situation in which we can't prevent the application from
     * making conflicting calls.
     *
     * These _r functions require that a buffer be supplied for the user and
     * group information, however.  These buffers may be unconfortably large
     * for the stack, so we malloc them on a static pointer and then mutex
     * lock this function so that it can still be reentrant.
     */

    gen_mutex_lock(&check_group_mutex);

    if(!check_group_pw_buffer)
    {
        /* need to create a buffer for pw and grp entries  */
#if 0
	/* The sysconf calls don't give a good buffer size back in every 
         * situation. It doesn't appear to know enough info on LDAP accounts 
         * to pick a good buffersize
         */
         

#if defined(_SC_GETGR_R_SIZE_MAX) && defined(_SC_GETPW_R_SIZE_MAX)
        /* newish posix systems can tell us what the max buffer size is */
        check_group_pw_buffer_size = sysconf(_SC_GETPW_R_SIZE_MAX);
#else
        /* fall back for older systems */
        check_group_pw_buffer_size = 1024;
#endif

#endif /* 0 */

        check_group_pw_buffer = (char*)malloc(pw_buf_size);
        usr_groups = (gid_t *)malloc(max_usr_groups * sizeof(gid_t));
        if(!check_group_pw_buffer || !usr_groups)
        {
            if(check_group_pw_buffer)
            {
                free(check_group_pw_buffer);
                check_group_pw_buffer = NULL;
            }
            if(usr_groups)
            {
                free(usr_groups);
                usr_groups = NULL;
            }
            gen_mutex_unlock(&check_group_mutex);
            return(-PVFS_ENOMEM);
        }
    }

    /* get user information */
    ret = getpwuid_r(uid, &pwd, check_group_pw_buffer, pw_buf_size, &pwd_p);
    if(ret != 0 || pwd_p == NULL)
    {
        gen_mutex_unlock(&check_group_mutex);
	gossip_err("Get user info for (uid=%d) failed."
                   "errno [%d] error_msg [%s]\n",
		   uid, ret, strerror(ret));
        return(-PVFS_EINVAL);
    }

    /* check primary group */
    if(pwd.pw_gid == gid)
    {
        gen_mutex_unlock(&check_group_mutex);
        return 0;
    }

    /* get the groups this member is part of */
    ret = getgrouplist(pwd.pw_name, pwd.pw_gid, usr_groups, &num_usr_groups);
    if(ret == -1)
    {
	gen_mutex_unlock(&check_group_mutex);
	gossip_err("Get groups for (uid=%d) failed. Requested up to [%d] groups"
                   "and found [%d]\n",
		   uid, max_usr_groups, num_usr_groups);
        return(-PVFS_EINVAL);
    }

    for(i = 0; i < num_usr_groups; i++)
    {
        if( usr_groups[i] == gid) 
        {
            gen_mutex_unlock(&check_group_mutex);
            return 0;
        } 
    }

    gen_mutex_unlock(&check_group_mutex);
    return(-PVFS_ENOENT);
}

/* Checks if a given user is part of any groups that matches the file gid */
static int in_group_p(PVFS_uid uid, PVFS_gid gid, PVFS_gid attr_group)
{
    if (attr_group == gid)
        return 1;
    if (PINT_check_group(uid, attr_group) == 0)
        return 1;
    return 0;
}

/*
 * Return 0 if requesting clients is granted want access to the object
 * by the acl. Returns -PVFS_E... otherwise.
 */
int PINT_check_acls(void *acl_buf, size_t acl_size, 
    PVFS_object_attr *attr,
    PVFS_uid uid, PVFS_gid gid, int want)
{
    pvfs2_acl_entry pe, *pa;
    int i = 0, found = 0, count = 0;
    assert(attr->mask & PVFS_ATTR_COMMON_UID &&
           attr->mask & PVFS_ATTR_COMMON_GID &&
           attr->mask & PVFS_ATTR_COMMON_PERM);

    if (acl_size == 0)
    {
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "no acl's present.. denying access\n");
        return -PVFS_EACCES;
    }

    /* keyval for ACLs includes a \0. so subtract the thingie */
    acl_size--;
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "PINT_check_acls: read keyval size "
    " %d (%d acl entries)\n",
        (int) acl_size, 
        (int) (acl_size / sizeof(pvfs2_acl_entry)));
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "uid = %d, gid = %d, want = %d\n",
        uid, gid, want);

    assert(acl_buf);
    /* if the acl format doesn't look valid, then return an error rather than
     * asserting; we don't want the server to crash due to an invalid keyval
     */
    if((acl_size % sizeof(pvfs2_acl_entry)) != 0)
    {
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "invalid acls on object\n");
        return(-PVFS_EACCES);
    }
    count = acl_size / sizeof(pvfs2_acl_entry);

    for (i = 0; i < count; i++)
    {
        pa = (pvfs2_acl_entry *) acl_buf + i;
        /* 
           NOTE: Remember that keyval is encoded as lebf, so convert it 
           to host representation 
        */
        pe.p_tag  = bmitoh32(pa->p_tag);
        pe.p_perm = bmitoh32(pa->p_perm);
        pe.p_id   = bmitoh32(pa->p_id);
        pa = &pe;
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "Decoded ACL entry %d "
            "(p_tag %d, p_perm %d, p_id %d)\n",
            i, pa->p_tag, pa->p_perm, pa->p_id);
        switch(pa->p_tag) 
        {
            case PVFS2_ACL_USER_OBJ:
                /* (May have been checked already) */
                if (attr->owner == uid)
                    goto check_perm;
                break;
            case PVFS2_ACL_USER:
                if (pa->p_id == uid)
                    goto mask;
                break;
            case PVFS2_ACL_GROUP_OBJ:
                if (in_group_p(uid, gid, attr->group)) 
                {
                    found = 1;
                    if ((pa->p_perm & want) == want)
                        goto mask;
                }
                break;
            case PVFS2_ACL_GROUP:
                if (in_group_p(uid, gid, pa->p_id)) {
                    found = 1;
                    if ((pa->p_perm & want) == want)
                        goto mask;
                }
                break;
            case PVFS2_ACL_MASK:
                break;
            case PVFS2_ACL_OTHER:
                if (found)
                {
                    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "(1) PINT_check_acls:"
                        "returning access denied\n");
                    return -PVFS_EACCES;
                }
                else
                    goto check_perm;
            default:
                gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "(2) PINT_check_acls: "
                        "returning EIO\n");
                return -PVFS_EIO;
        }
    }
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "(3) PINT_check_acls: returning EIO\n");
    return -PVFS_EIO;
mask:
    /* search the remaining entries */
    i = i + 1;
    for (; i < count; i++)
    {
        pvfs2_acl_entry me, *mask_obj = (pvfs2_acl_entry *) acl_buf + i;
        
        /* 
          NOTE: Again, since pvfs2_acl_entry is in lebf, we need to
          convert it to host endian format
         */
        me.p_tag  = bmitoh32(mask_obj->p_tag);
        me.p_perm = bmitoh32(mask_obj->p_perm);
        me.p_id   = bmitoh32(mask_obj->p_id);
        mask_obj = &me;
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "Decoded (mask) ACL entry %d "
            "(p_tag %d, p_perm %d, p_id %d)\n",
            i, mask_obj->p_tag, mask_obj->p_perm, mask_obj->p_id);
        if (mask_obj->p_tag == PVFS2_ACL_MASK) 
        {
            if ((pa->p_perm & mask_obj->p_perm & want) == want)
                return 0;
            gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "(4) PINT_check_acls:"
                "returning access denied (mask)\n");
            return -PVFS_EACCES;
        }
    }

check_perm:
    if ((pa->p_perm & want) == want)
        return 0;
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "(5) PINT_check_acls: returning"
            "access denied\n");
    return -PVFS_EACCES;
}

