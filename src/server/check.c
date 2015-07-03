/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Changes by Acxiom Corporation to add PINT_check_mode() helper function
 * as a replacement for check_mode() in permission checking, also added
 * PINT_check_group() for supplimental group support 
 * Copyright Acxiom Corporation, 2005.
 *
 * See COPYING in top-level directory.
 */

/*
 * Server-specific utility functions, to check modes and ACLs.
 */
#include <string.h>

#include "pvfs2-internal.h"
#include "pvfs2-debug.h"
#include "pvfs2-server.h"
#include "pvfs2-attr.h"
#include "server-config.h"
#include "trove.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include "pint-perf-counter.h"
#include "gen-locks.h"
#include "gossip.h"
#include "bmi-byteswap.h"
#include "check.h"
#include "security-util.h"

enum access_type
{
    READ_ACCESS,
    WRITE_ACCESS,
    EXEC_ACCESS
};

static int check_mode(enum access_type access, PVFS_uid userid,
    PVFS_gid group, const PVFS_object_attr *attr);
static int check_acls(void *acl_buf, size_t acl_size, 
    const PVFS_object_attr *attr, PVFS_uid uid, PVFS_gid *group_array, 
    uint32_t num_groups, int want);
static int check_seteattr_dir_hint(struct PVFS_servreq_seteattr *seteattr);

/* PINT_get_capabilities
 *
 * Sets all of the capability bits in the op_mask that apply to
 * the combination of the given user and permissions. UID 0 is
 * given all possible capabilities.
 *
 * returns 0 on success
 * returns negative PVFS error on failure
 */
int PINT_get_capabilities(void *acl_buf, 
                          size_t acl_size, 
                          PVFS_uid userid,
                          PVFS_gid *group_array, 
                          uint32_t num_groups,
                          const PVFS_object_attr *attr, 
                          uint32_t *op_mask)
{
    PVFS_gid active_group;
    int ret;
    int i;

    *op_mask = 0;
    
    /* root has every possible capability */
    if (userid == 0)
    {
        *op_mask = ~((uint32_t)0);
        /* remove dir-only capabilities */
        if (attr->objtype != PVFS_TYPE_DIRECTORY)
        {
            *op_mask &= ~(PINT_CAP_CREATE|PINT_CAP_REMOVE);
        }
        return 0;
    }

    /* if acls are present then use them */
    if (acl_size > 0)
    {
        if (acl_buf == NULL)
        {
            gossip_err("%s: null ACL buffer\n", __func__);
            return -PVFS_EINVAL;
        }

        /* nlmills: errors are ignored on purpose. we don't want to
           give anyone free access.
        */

        ret = check_acls(acl_buf, acl_size, attr, userid, 
                         group_array, num_groups, PVFS2_ACL_READ);
        if (ret > 0)
        {
            *op_mask |= PINT_CAP_READ;
        }
        else if (ret < 0)
        {
            return ret;
        }

        ret = check_acls(acl_buf, acl_size, attr, userid,
                         group_array, num_groups, PVFS2_ACL_WRITE);
        if (ret > 0)
        {
            *op_mask |= PINT_CAP_WRITE;
        }
        else if (ret < 0)
        {
            return ret;
        }

        ret = check_acls(acl_buf, acl_size, attr, userid,
                         group_array, num_groups, PVFS2_ACL_EXECUTE);
        if (ret > 0)
        {
            *op_mask |= PINT_CAP_EXEC;
        }
        else if (ret < 0)
        {
            return ret;
        }
    }
    /* otherwise fall back to standard UNIX permissions */
    else
    {
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG,
                     "PINT_get_capabilities: ACL unavailable, "
                     "using UNIX permissions\n");
        
        if (num_groups == 0 || !(attr->mask & PVFS_ATTR_COMMON_GID))
        {
            gossip_err("%s: no groups or PVFS_ATTR_COMMON_GID not set\n", 
                       __func__);
            return -PVFS_EINVAL;
        }

        /* see if the user is a member of the object's group */
        active_group = group_array[0];
        for (i = 0; i < num_groups; i++)
        {
            if (group_array[i] == attr->group)
            {
                active_group = group_array[i];
                break;
            }
        }

        ret = check_mode(READ_ACCESS, userid, active_group, attr);
        if (ret > 0)
        {
            *op_mask |= PINT_CAP_READ;
        }
        else if (ret < 0)
        {
            return ret;
        }

        ret = check_mode(WRITE_ACCESS, userid, active_group, attr);
        if (ret > 0)
        {
            *op_mask |= PINT_CAP_WRITE;
        }
        else if (ret < 0)
        {
            return ret;
        }

        ret = check_mode(EXEC_ACCESS, userid, active_group, attr);
        if (ret > 0)
        {
            *op_mask |= PINT_CAP_EXEC;
        }
        else if (ret < 0)
        {
            return ret;
        }
    }

    /* only the owner can set attributes */
    if (userid == attr->owner)
    {
        *op_mask |= PINT_CAP_SETATTR;
    }

    /* write and exec access to directories allows create and remove */
    if (attr->objtype == PVFS_TYPE_DIRECTORY &&
        *op_mask & PINT_CAP_WRITE &&
        *op_mask & PINT_CAP_EXEC)
    {
        *op_mask |= PINT_CAP_CREATE | PINT_CAP_REMOVE;
    }

    return 0;
}

/* PINT_perm_check
 *
 * Performs a permission check for the given server operation. Calls
 * the permission function given in the server state machine.
 *
 * returns 0 on success
 * returns negative PVFS error on failure
 */
int PINT_perm_check(struct PINT_server_op *s_op)
{
    PVFS_capability *cap = &s_op->req->capability;
    PVFS_handle handle;
    PINT_server_req_perm_fun perm_fun;
    int ret = -PVFS_EINVAL, i;
    char op_mask[16];

    gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: checking operation %s\n", 
                 __func__, PINT_map_server_op_to_string(s_op->req->op));

    perm_fun = PINT_server_req_get_perm_fun(s_op->req);
    if (perm_fun == NULL)
    {
        gossip_err("%s: permission check for operation with no permission "
                   "function\n", __func__);

        return -PVFS_EINVAL;
    }

    /* do not check handles for null caps */
    if (!PINT_capability_is_null(cap))
    {                
        switch (s_op->req->op)
        {
            /* remove ops use parent handle from hint */
            case PVFS_SERV_REMOVE:
            case PVFS_SERV_TREE_REMOVE:
            /* io ops use metafile handle from hint */            
            case PVFS_SERV_SMALL_IO:
            case PVFS_SERV_IO:
                handle = PINT_HINT_GET_HANDLE(s_op->req->hints);
                if (handle == PVFS_HANDLE_NULL)
                {
                    gossip_err("%s: could not retrieve parent/metafile handle "
                               "from hint\n", __func__);
                    ret = -PVFS_EINVAL;
                    goto PINT_perm_check_exit;
                }
                break;
            /* seteattr uses parent handle for certain eattrs */
            case PVFS_SERV_SETEATTR:
                if (check_seteattr_dir_hint(&s_op->req->u.seteattr))
                {
                    handle = PINT_HINT_GET_HANDLE(s_op->req->hints);
                    if (handle == PVFS_HANDLE_NULL)
                    {
                        gossip_err("%s: could not retrieve parent handle from "
                                   "hint\n", __func__);
                        ret = -PVFS_EINVAL;
                        goto PINT_perm_check_exit;
                    }
                }
                else
                {
                    handle = s_op->target_handle;
                }
                break;
            default:
                handle = s_op->target_handle;
                break;
        }
/* TODO: remove
        if (handle == PVFS_HANDLE_NULL || fs_id == PVFS_FS_ID_NULL)
        {
            gossip_err("%s: operation %d has no handle/fs_id\n", __func__,
                       (int) s_op->req->op);
            return -PVFS_EINVAL;
        }
*/
        if (handle != PVFS_HANDLE_NULL)
        {
            gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: using operation handle %llu\n",
                         __func__, llu(handle));

            /* ensure we have a capability for the target handle */
            for (i = 0; i < cap->num_handles; i++)
            {
                if (cap->handle_array[i] == handle)
                {
                    break;
                }
            }
            if (i == cap->num_handles)
            {
                 gossip_err("%s: attempted to perform an operation on target "
                           "handle %llu that was not in the capability\n", 
                           __func__, llu(handle));
                 ret = -PVFS_EACCES;
                 goto PINT_perm_check_exit;
            }
        }
    }

    gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: perms %o (capability mask = %s)\n",
                 __func__, s_op->attr.perms, 
                 PINT_capability_is_null(cap) ? "[null]" : 
                     PINT_print_op_mask(cap->op_mask, op_mask));

    ret = perm_fun(s_op);

PINT_perm_check_exit:
    gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: returning %d\n", __func__, ret);

    return ret;
}

static int check_mode(enum access_type access, 
               PVFS_uid userid, 
               PVFS_gid group,
               const PVFS_object_attr *attr)
{
    int user_other_access = 0;
    int group_access = 0;
    int mask;
    
    if (!(attr->mask & PVFS_ATTR_COMMON_UID) ||
        !(attr->mask & PVFS_ATTR_COMMON_GID) ||
        !(attr->mask & PVFS_ATTR_COMMON_PERM))
    {
        gossip_err("%s: invalid mask\n", __func__);
        return -PVFS_EINVAL;
    }

    mask = 0;
    if (attr->owner == userid)
    {
        if (access == READ_ACCESS)
        {
            mask = PVFS_U_READ;
        }
        else if (access == WRITE_ACCESS)
        {
            mask = PVFS_U_WRITE;
        }
        else if (access == EXEC_ACCESS)
        {
            mask = PVFS_U_EXECUTE;
        }
    }
    else
    {
        if (access == READ_ACCESS)
        {
            mask = PVFS_O_READ;
        }
        else if (access == WRITE_ACCESS)
        {
            mask = PVFS_O_WRITE;
        }
        else if (access == EXEC_ACCESS)
        {
            mask = PVFS_O_EXECUTE;
        }
    }
    
    user_other_access = attr->perms & mask;

    mask = 0;
    if (attr->group == group)
    {
        if (access == READ_ACCESS)
        {
            mask = PVFS_G_READ;
        }
        else if (access == WRITE_ACCESS)
        {
            mask = PVFS_G_WRITE;
        }
        else if (access == EXEC_ACCESS)
        {
            mask = PVFS_G_EXECUTE;
        }
    }

    group_access = attr->perms & mask;
    
    return (user_other_access || group_access);
}

/*
 * Return 0 if requesting clients is granted want access to the object
 * by the acl. Returns -PVFS_EINVAL if ACL is invalid or not found,
 * returns -PVFS_EACCESS if access is denied
 */
static int check_acls(void *acl_buf, 
                      size_t acl_size, 
                      const PVFS_object_attr *attr,
                      PVFS_uid uid,
                      PVFS_gid *group_array, 
                      uint32_t num_groups, 
                      int want)
{
#ifndef PVFS_USE_OLD_ACL_FORMAT
    pvfs2_acl_header *ph;
#endif
    pvfs2_acl_entry pe, *pa;
    int i = 0, j = 0, found = 0, count = 0;

    if (!(attr->mask & PVFS_ATTR_COMMON_UID) ||
        !(attr->mask & PVFS_ATTR_COMMON_GID) ||
        !(attr->mask & PVFS_ATTR_COMMON_PERM) ||
        num_groups == 0 ||
        acl_size == 0 ||
        acl_buf == NULL)
    {
        gossip_err("%s: invalid argument\n", __func__);
        return -PVFS_EINVAL;
    }

    /* keyval for ACLs includes a \0. so subtract the thingie */
    acl_size--;
#ifndef PVFS_USE_OLD_ACL_FORMAT
    /* remove header when calculating size */
    acl_size -= sizeof(pvfs2_acl_header);
#endif
    /* if the acl format doesn't look valid, then return an error rather than
     * asserting; we don't want the server to crash due to an invalid keyval
     */
    if ((acl_size % sizeof(pvfs2_acl_entry)) != 0)
    {
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "%s: invalid acls on object\n",
                     __func__);
        return -PVFS_EINVAL;
    }
    count = acl_size / sizeof(pvfs2_acl_entry);

    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "%s: read keyval size "
        " %d (%d acl entries)\n", __func__, (int) acl_size, count);
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "%s: uid = %d, gid = %d, want = %d\n",
        __func__, uid, group_array[0], want);


#ifndef PVFS_USE_OLD_ACL_FORMAT
    /* point to header */
    ph = (pvfs2_acl_header *) acl_buf;
#endif

    for (i = 0; i < count; i++)
    {
#ifdef PVFS_USE_OLD_ACL_FORMAT
        pa = (pvfs2_acl_entry *) acl_buf + i;
#else        
        pa = &(ph->p_entries[i]);
#endif
        /* 
           NOTE: Remember that keyval is encoded as lebf, so convert it 
           to host representation 
        */
        pe.p_tag  = bmitoh32(pa->p_tag);
        pe.p_perm = bmitoh32(pa->p_perm);
        pe.p_id   = bmitoh32(pa->p_id);
        pa = &pe;
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "%s: decoded acl entry %d "
            "(p_tag %d, p_perm %d, p_id %d)\n",
            __func__, i, pa->p_tag, pa->p_perm, pa->p_id);
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
                for (j = 0; j < num_groups; j++)
                {
                    if (group_array[j] == attr->group)
                    {
                        found = 1;
                        if ((pa->p_perm & want) == want)
                            goto mask;
                        break;
                    }
                }
                break;
            case PVFS2_ACL_GROUP:
                for (j = 0; j < num_groups; j++)
                {
                    if (group_array[j] == pa->p_id)
                    {
                        found = 1;
                        if ((pa->p_perm & want) == want)
                            goto mask;
                        break;
                    }
                }
                break;
            case PVFS2_ACL_MASK:
                break;
            case PVFS2_ACL_OTHER:
                if (found)
                {
                    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "%s: returning "
                                 "EINVAL (1)\n", __func__);
                    return -PVFS_EINVAL;
                }
                else
                    goto check_perm;
            default:
                gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "%s: returning "
                             "EINVAL (2)\n", __func__);
                return -PVFS_EINVAL;
        }
    }
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "%s: returning EINVAL (3)\n", 
                 __func__);
    return -PVFS_EINVAL;
mask:
    /* search the remaining entries */
    i = i + 1;
    for (; i < count; i++)
    {
#ifdef PVFS_USE_OLD_ACL_FORMAT
        pvfs2_acl_entry me, *mask_obj = (pvfs2_acl_entry *) acl_buf + i;
#else
        pvfs2_acl_entry me, *mask_obj = &(ph->p_entries[i]);        
#endif
        /* 
          NOTE: Again, since pvfs2_acl_entry is in lebf, we need to
          convert it to host endian format
         */
        me.p_tag  = bmitoh32(mask_obj->p_tag);
        me.p_perm = bmitoh32(mask_obj->p_perm);
        me.p_id   = bmitoh32(mask_obj->p_id);
        mask_obj = &me;
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "%s: decoded (mask) acl entry %d "
            "(p_tag %d, p_perm %d, p_id %d)\n", __func__, i, mask_obj->p_tag, 
            mask_obj->p_perm, mask_obj->p_id);
        if (mask_obj->p_tag == PVFS2_ACL_MASK) 
        {
            if ((pa->p_perm & mask_obj->p_perm & want) == want)
                return 0;
            gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "%s: returning EACCES (mask)\n",
                         __func__);
            return -PVFS_EACCES;
        }
    }

check_perm:
    if ((pa->p_perm & want) == want)
        return 0;
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "%s: returning EACCES\n",
                 __func__);
    return -PVFS_EACCES;
}

/* Returns true if all the eattrs are dir hints (see 
   mkdir_seteattr_setup_msgpair in sys-mkdir.sm). */
static int check_seteattr_dir_hint(struct PVFS_servreq_seteattr *seteattr)
{
    int i;

    if (seteattr->nkey == 0)
    {
        gossip_err("Warning: seteattr operation with no keys\n");
        return 0;
    }

    for (i = 0; i < seteattr->nkey; i++)
    {
        if (strcmp((char *) seteattr->key[i].buffer, "user.pvfs2.num_dfiles") ||
            strcmp((char *) seteattr->key[i].buffer, "user.pvfs2.dist_name") ||
            strcmp((char *) seteattr->key[i].buffer, "user.pvfs2.dist_params"))
        {
            return 0;
        }
    }

    return 1;
}
/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
