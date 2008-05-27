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
#include "pvfs2-server.h"
#include "pvfs2-attr.h"
#include "server-config.h"
#include "src/server/request-scheduler/request-scheduler.h"
#include "trove.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include "pint-perf-counter.h"
#include "gen-locks.h"
#include "gossip.h"
#include "bmi-byteswap.h"
#include "check.h"

enum {
    PRELUDE_RUN_ACL_CHECKS = 1,
};

static gen_mutex_t check_group_mutex = GEN_MUTEX_INITIALIZER;
static char* check_group_pw_buffer = NULL;
static long check_group_pw_buffer_size = 0;
static char* check_group_gr_buffer = NULL;
static long check_group_gr_buffer_size = 0;
static int PINT_check_group(uid_t uid, gid_t gid);
static int iterate_ro_wildcards(struct filesystem_configuration_s *fsconfig, 
    PVFS_BMI_addr_t client_addr);
static int iterate_root_squash_wildcards(struct filesystem_configuration_s *fsconfig,
    PVFS_BMI_addr_t client_addr);
static int iterate_all_squash_wildcards(struct filesystem_configuration_s *fsconfig,
    PVFS_BMI_addr_t client_addr);
static void get_anon_ids(struct filesystem_configuration_s *fsconfig,
    PVFS_uid *uid, PVFS_gid *gid);
static int translate_ids(PVFS_fs_id fsid, PVFS_uid uid, PVFS_gid gid, 
    PVFS_uid *translated_uid, PVFS_gid *translated_gid, 
    PVFS_BMI_addr_t client_addr);
static int permit_operation(PVFS_fs_id fsid,
    enum PINT_server_req_access_type access_type, PVFS_BMI_addr_t client_addr);

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
    struct group grp;
    struct group* grp_p = NULL;
    int i = 0;
    int ret = -1;

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
        /* need to create a buffer for pw and grp entries */
#if defined(_SC_GETGR_R_SIZE_MAX) && defined(_SC_GETPW_R_SIZE_MAX)
        /* newish posix systems can tell us what the max buffer size is */
        check_group_gr_buffer_size = sysconf(_SC_GETGR_R_SIZE_MAX);
        check_group_pw_buffer_size = sysconf(_SC_GETPW_R_SIZE_MAX);
#else
        /* fall back for older systems */
        check_group_pw_buffer_size = 1024;
        check_group_gr_buffer_size = 1024;
#endif
        check_group_pw_buffer = (char*)malloc(check_group_pw_buffer_size);
        check_group_gr_buffer = (char*)malloc(check_group_gr_buffer_size);
        if(!check_group_pw_buffer || !check_group_gr_buffer)
        {
            if(check_group_pw_buffer)
            {
                free(check_group_pw_buffer);
                check_group_pw_buffer = NULL;
            }
            if(check_group_gr_buffer)
            {
                free(check_group_gr_buffer);
                check_group_gr_buffer = NULL;
            }
            gen_mutex_unlock(&check_group_mutex);
            return(-PVFS_ENOMEM);
        }
    }

    /* get user information */
    ret = getpwuid_r(uid, &pwd, check_group_pw_buffer,
        check_group_pw_buffer_size,
        &pwd_p);
    if(ret != 0 || pwd_p == NULL)
    {
        gen_mutex_unlock(&check_group_mutex);
        return(-PVFS_EINVAL);
    }

    /* check primary group */
    if(pwd.pw_gid == gid)
    {
        gen_mutex_unlock(&check_group_mutex);
        return 0;
    }

    /* get other group information */
    ret = getgrgid_r(gid, &grp, check_group_gr_buffer,
        check_group_gr_buffer_size,
        &grp_p);
    if(ret != 0)
    {
        gen_mutex_unlock(&check_group_mutex);
        return(-PVFS_EINVAL);
    }

    if(grp_p == NULL)
    { 
	gen_mutex_unlock(&check_group_mutex);
	gossip_err("User (uid=%d) isn't in group %d on storage node.\n",
		   uid, gid);
        return(-PVFS_EACCES);
    }

    for(i = 0; grp.gr_mem[i] != NULL; i++)
    {
        if(0 == strcmp(pwd.pw_name, grp.gr_mem[i]) )
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


/* PINT_server_get_perm()
 *
 * this really just marks the spot where we would want to do
 * permission checking, it will be replaced by a couple of states that
 * actually perform this task later
 */
static PINT_sm_action prelude_perm_check(
        struct PINT_smcb *smcb, job_status_s *js_p)
{
    struct PINT_server_op *s_op = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    PVFS_object_attr *obj_attr = NULL;
    PVFS_ds_attributes *ds_attr = NULL;
    PVFS_uid translated_uid = s_op->req->credentials.uid;
    PVFS_gid translated_gid = s_op->req->credentials.gid;
    PVFS_fs_id  fsid = PVFS_FS_ID_NULL;
    int squashed_flag = 0;
    int skip_acl_flag = 0;

    /* moved gossip server debug output to end of state, so we can report
     * resulting status value.
     */

    /*
      first we translate the dspace attributes into a more convenient
      server use-able format.  i.e. a PVFS_object_attr
    */
    ds_attr = &s_op->ds_attr;
    obj_attr = &s_op->attr;
    PVFS_ds_attr_to_object_attr(ds_attr, obj_attr);
    s_op->attr.mask = PVFS_ATTR_COMMON_ALL;
    /* Set the target object attribute pointer.. used later by the acl check */
    s_op->target_object_attr = obj_attr;

    if (s_op->target_fs_id != PVFS_FS_ID_NULL)
    {
        /*
         * if we are exporting a volume readonly, disallow any operation that modifies
         * the state of the file-system.
         */
        if (permit_operation(
                s_op->target_fs_id, s_op->access_type, s_op->addr) < 0)
        {
            js_p->error_code = -PVFS_EROFS;
            return SM_ACTION_COMPLETE;
        }
        else 
        {
            /* Translate the uid and gid's in case we need to do some squashing based on the export and the client address */
            if (translate_ids(fsid, s_op->req->credentials.uid, s_op->req->credentials.gid,
                &translated_uid, &translated_gid, s_op->addr) == 1)
            {
                squashed_flag = 1;
                s_op->req->credentials.uid = translated_uid;
                s_op->req->credentials.gid = translated_gid;
                /* in the case of a setattr, translate the ids as well right here */
                if (s_op->req->op == PVFS_SERV_SETATTR)
                {
                    s_op->req->u.setattr.attr.owner = translated_uid;
                    s_op->req->u.setattr.attr.group = translated_gid;
                }
                else if (s_op->req->op == PVFS_SERV_MKDIR)
                {
                    s_op->req->u.mkdir.attr.owner = translated_uid;
                    s_op->req->u.mkdir.attr.group = translated_gid;
                }
            }
       }
    }

    /* anything else we treat as a real error */
    if (js_p->error_code)
    {
        js_p->error_code = -PVFS_ERROR_CODE(-js_p->error_code);
        return SM_ACTION_COMPLETE;
    }

    gossip_debug(
        GOSSIP_PERMISSIONS_DEBUG, "PVFS operation \"%s\" got "
        "attr mask %d\n\t(attr_uid_valid? %s, attr_owner = "
        "%d, credentials_uid = %d)\n\t(attr_gid_valid? %s, attr_group = "
        "%d, credentials.gid = %d)\n",
        PINT_map_server_op_to_string(s_op->req->op), s_op->attr.mask,
        ((s_op->attr.mask & PVFS_ATTR_COMMON_UID) ? "yes" : "no"),
        s_op->attr.owner, translated_uid,
        ((s_op->attr.mask & PVFS_ATTR_COMMON_GID) ? "yes" : "no"),
        s_op->attr.group, translated_gid);
    
    switch(PINT_server_req_get_perms(s_op->req))
    {
        case PINT_SERVER_CHECK_WRITE:
            js_p->error_code = PINT_check_mode(
                &(s_op->attr), translated_uid,
                translated_gid, PINT_ACCESS_WRITABLE);
            break;
        case PINT_SERVER_CHECK_READ:
            js_p->error_code = PINT_check_mode(
                &(s_op->attr), translated_uid,
                translated_gid, PINT_ACCESS_READABLE);
            break;
        case PINT_SERVER_CHECK_CRDIRENT:
            /* must also check executable after writable */
            js_p->error_code = PINT_check_mode(
                &(s_op->attr), translated_uid,
                translated_gid, PINT_ACCESS_WRITABLE);
            if(js_p->error_code == 0)
            {
                js_p->error_code = PINT_check_mode(
                    &(s_op->attr), translated_uid,
                    translated_gid, PINT_ACCESS_EXECUTABLE);
            }
            break;
        case PINT_SERVER_CHECK_ATTR:
            /* let datafiles pass through the attr check */
            if (s_op->attr.objtype == PVFS_TYPE_DATAFILE)
            {
                js_p->error_code = 0;
            }
            /* for now we'll assume extended attribs are treated
             * the same as regular attribs as far as permissions
             */
	    else if (s_op->req->op == PVFS_SERV_GETATTR ||
                    s_op->req->op == PVFS_SERV_GETEATTR ||
                    s_op->req->op == PVFS_SERV_LISTEATTR)
	    {
		/* getting or listing attributes is always ok -- permission
		 * is checked on the parent directory at read time
		 */
		js_p->error_code = 0;
	    }
            else /* setattr, seteattr, seteattr_list */
            {
                /*
                  NOTE: on other file systems, setattr doesn't
                  seem to require read permissions by the user, group
                  OR other, so long as the user or group matches (or
                  is root)
                */
                if (((s_op->attr.mask & PVFS_ATTR_COMMON_UID) &&
                     ((s_op->attr.owner == 0) ||
                      (s_op->attr.owner == translated_uid))) ||
                    (((s_op->attr.mask & PVFS_ATTR_COMMON_GID) &&
                      ((s_op->attr.group == 0) ||
                       (s_op->attr.group == translated_gid)))) ||
                    (translated_uid == 0))
                {
                    js_p->error_code = 0;
                }
                else
                {
                    js_p->error_code = -PVFS_EACCES;
                }
            }
            break;
        case PINT_SERVER_CHECK_NONE:
            if(squashed_flag &&
               PINT_server_req_get_access_type(s_op->req) == PINT_SERVER_REQ_MODIFY &&
               ((s_op->req->op == PVFS_SERV_IO) ||
                (s_op->req->op == PVFS_SERV_SMALL_IO) ||
                (s_op->req->op == PVFS_SERV_TRUNCATE)))
            {
                /* special case:
                 * If we have been squashed, deny write permission to the
                 * file system.  At the datafile level we don't have enough
                 * attribute information to figure out if the nobody/guest
                 * user has permission to write or not, so we disallow all
                 * writes to be safe.  Not perfect semantics, but better
                 * than being too permissive.
                 */
                skip_acl_flag = 1;
                js_p->error_code = -PVFS_EACCES;
            }
            else
            {
                js_p->error_code = 0;
            }
            break;
        case PINT_SERVER_CHECK_INVALID:
            js_p->error_code = -PVFS_EINVAL;
            break;
    }

    gossip_debug(
        GOSSIP_PERMISSIONS_DEBUG, "Final permission check for \"%s\" set "
        "error code to %d\n", PINT_map_server_op_to_string(s_op->req->op),
        js_p->error_code);

    gossip_debug(GOSSIP_SERVER_DEBUG, 
        "(%p) %s (prelude sm) state: perm_check (status = %d)\n",
	s_op,
        PINT_map_server_op_to_string(s_op->req->op),
	js_p->error_code);
    /* If regular checks fail, we need to run acl checks */
    if (js_p->error_code == -PVFS_EACCES && !skip_acl_flag)
        js_p->error_code = PRELUDE_RUN_ACL_CHECKS;
    return SM_ACTION_COMPLETE;
}

/*
 * Return zero if this operation should be allowed.
 */
static int permit_operation(PVFS_fs_id fsid,
                            enum PINT_server_req_access_type access_type,
                            PVFS_BMI_addr_t client_addr)
{ 
    int exp_flags = 0; 
    struct server_configuration_s *serv_config = NULL;
    struct filesystem_configuration_s * fsconfig = NULL;

    if (access_type == PINT_SERVER_REQ_READONLY)
    {
        return 0;  /* anything that doesn't modify state is okay */
    }
    serv_config = PINT_get_server_config();
    fsconfig = PINT_config_find_fs_id(serv_config, fsid);

    if (fsconfig == NULL)
    {
        return 0;
    }
    exp_flags = fsconfig->exp_flags;

    /* cheap test to see if ReadOnly was even specified in the exportoptions */
    if (!(exp_flags & TROVE_EXP_READ_ONLY))
    {
        return 0;
    }
    /* Drat. Iterate thru the list of wildcards specified in server_configuration and see
     * the client address matches. if yes, then we deny permission
     */
    if (iterate_ro_wildcards(fsconfig, client_addr) == 1)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, 
            "Disallowing read-write operation on a read-only exported file-system\n");
        return -EROFS;
    }
    return 0;
}

/* Translate_ids will return 1 if it did some uid/gid squashing, 0 otherwise */
static int translate_ids(PVFS_fs_id fsid, PVFS_uid uid, PVFS_gid gid, 
    PVFS_uid *translated_uid, PVFS_gid *translated_gid, PVFS_BMI_addr_t client_addr)
{
    int exp_flags = 0;
    struct server_configuration_s *serv_config = NULL;
    struct filesystem_configuration_s * fsconfig = NULL;

    serv_config = PINT_get_server_config();
    fsconfig = PINT_config_find_fs_id(serv_config, fsid);

    if (fsconfig == NULL)
    {
        return 0;
    }
    exp_flags = fsconfig->exp_flags;
    /* If all squash was set */
    if (exp_flags & TROVE_EXP_ALL_SQUASH)
    {
        if (iterate_all_squash_wildcards(fsconfig, client_addr) == 1)
        {
            get_anon_ids(fsconfig, translated_uid, translated_gid);
            gossip_debug(GOSSIP_SERVER_DEBUG,
                "Translated ids from <%u:%u> to <%u:%u>\n",
                uid, gid, *translated_uid, *translated_gid);
            return 1;
        }
    }
    /* if only root squash was set translate uids for root alone*/
    if (exp_flags & TROVE_EXP_ROOT_SQUASH)
    {
        if (uid == 0 || gid == 0)
        {
            if (iterate_root_squash_wildcards(fsconfig, client_addr) == 1)
            {
                get_anon_ids(fsconfig, translated_uid, translated_gid);
                gossip_debug(GOSSIP_SERVER_DEBUG,
                    "Translated ids from <%u:%u> to <%u:%u>\n",
                    uid, gid, *translated_uid, *translated_gid);
                return 1;
            }
        }
    }
    /* no such translation required! */
    *translated_uid = uid;
    *translated_gid = gid;
    return 0;
}

static void get_anon_ids(struct filesystem_configuration_s *fsconfig,
    PVFS_uid *uid, PVFS_gid *gid)
{
    *uid = fsconfig->exp_anon_uid;
    *gid = fsconfig->exp_anon_gid;
    return;
}

static int iterate_all_squash_wildcards(struct filesystem_configuration_s *fsconfig,
    PVFS_BMI_addr_t client_addr)
{
    int i;

    for (i = 0; i < fsconfig->all_squash_count; i++)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "BMI_query_addr_range %lld, %s\n",
            lld(client_addr), fsconfig->all_squash_hosts[i]);
        if (BMI_query_addr_range(client_addr, fsconfig->all_squash_hosts[i],
                fsconfig->all_squash_netmasks[i]) == 1)
        {
            return 1;
        }
    }
    return 0;
}

static int iterate_root_squash_wildcards(struct filesystem_configuration_s *fsconfig,
    PVFS_BMI_addr_t client_addr)
{
    int i;

    /* check exceptions first */
    for (i = 0; i < fsconfig->root_squash_exceptions_count; i++)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "BMI_query_addr_range %lld, %s, netmask: %i\n",
            lld(client_addr), fsconfig->root_squash_exceptions_hosts[i],
            fsconfig->root_squash_exceptions_netmasks[i]);
        if (BMI_query_addr_range(client_addr, fsconfig->root_squash_exceptions_hosts[i], 
                fsconfig->root_squash_exceptions_netmasks[i]) == 1)
        {
            /* in the exception list, do not squash */
            return 0;
        }
    }

    for (i = 0; i < fsconfig->root_squash_count; i++)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "BMI_query_addr_range %lld, %s, netmask: %i\n",
            lld(client_addr), fsconfig->root_squash_hosts[i],
            fsconfig->root_squash_netmasks[i]);
        if (BMI_query_addr_range(client_addr, fsconfig->root_squash_hosts[i], 
                fsconfig->root_squash_netmasks[i]) == 1)
        {
            return 1;
        }
    }
    return 0;
}

static int iterate_ro_wildcards(struct filesystem_configuration_s *fsconfig, PVFS_BMI_addr_t client_addr)
{
    int i;

    for (i = 0; i < fsconfig->ro_count; i++)
    {
        gossip_debug(GOSSIP_SERVER_DEBUG, "BMI_query_addr_range %lld, %s\n",
            lld(client_addr), fsconfig->ro_hosts[i]);
        /* Does the client address match the wildcard specification and/or the netmask specification? */
        if (BMI_query_addr_range(client_addr, fsconfig->ro_hosts[i],
                fsconfig->ro_netmasks[i]) == 1)
        {
            return 1;
        }
    }
    return 0;
}

static PINT_sm_action prelude_check_acls_if_needed(
    struct PINT_smcb *smcb, job_status_s *js_p)
{
    struct PINT_server_op *s_op = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    int ret = -PVFS_EINVAL;
    job_id_t i;

    gossip_debug(GOSSIP_SERVER_DEBUG,
                 "(%p) %s (prelude sm) state: prelude_check_acls_if_needed\n", s_op,
                 PINT_map_server_op_to_string(s_op->req->op));

    /* If we get here with an invalid fsid and handle, we have to
     * return -PVFS_EACCESS 
     */
    if (s_op->target_fs_id == PVFS_FS_ID_NULL
        || s_op->target_handle == PVFS_HANDLE_NULL)
    {
        js_p->error_code = -PVFS_EACCES;
        return SM_ACTION_COMPLETE;
    }
    js_p->error_code = 0;

    memset(&s_op->key, 0, sizeof(PVFS_ds_keyval));
    memset(&s_op->val, 0, sizeof(PVFS_ds_keyval));
    s_op->key.buffer = "system.posix_acl_access";
    s_op->key.buffer_sz = strlen(s_op->key.buffer) + 1;
    s_op->val.buffer = (char *) malloc(PVFS_REQ_LIMIT_VAL_LEN);
    if (!s_op->val.buffer)
    {
        js_p->error_code = -PVFS_ENOMEM;
        return SM_ACTION_COMPLETE;
    }
    s_op->val.buffer_sz = PVFS_REQ_LIMIT_VAL_LEN;

    gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "About to retrieve acl keyvals "
                 "for handle %llu\n", llu(s_op->target_handle));

    /* Read acl keys */
    ret = job_trove_keyval_read(
        s_op->target_fs_id,
        s_op->target_handle,
        &s_op->key,
        &s_op->val,
        0,
        NULL,
        smcb,
        0,
        js_p,
        &i,
        server_job_context);
    return ret;
}

static PINT_sm_action prelude_check_acls(
    struct PINT_smcb *smcb, job_status_s *js_p)
{
    struct PINT_server_op *s_op = PINT_sm_frame(smcb, PINT_FRAME_CURRENT);
    PVFS_object_attr *obj_attr = NULL;
    int want = 0;

    /* The dspace attr must have been read at this point */
    obj_attr = s_op->target_object_attr;
    assert(obj_attr);

    /* anything non-zero we treat as a real error */
    if (js_p->error_code)
    {
        goto cleanup;
    }
    /* make sure that we hit here only for metafiles, dirs and symlink objects */
    if (obj_attr->objtype != PVFS_TYPE_METAFILE
        && obj_attr->objtype != PVFS_TYPE_DIRECTORY
        && obj_attr->objtype != PVFS_TYPE_SYMLINK)
    {
        gossip_err("prelude_check_acls hit invalid object type %d\n",
            obj_attr->objtype);
        js_p->error_code = -PVFS_EINVAL;
        goto cleanup;
    }
    switch (PINT_server_req_get_perms(s_op->req))
    {
        case PINT_SERVER_CHECK_WRITE:
        default:
            want = PVFS2_ACL_WRITE;
            break;
        case PINT_SERVER_CHECK_READ:
            want = PVFS2_ACL_READ;
            break;
        case PINT_SERVER_CHECK_CRDIRENT:
            want = PVFS2_ACL_WRITE | PVFS2_ACL_EXECUTE;
            break;
        case PINT_SERVER_CHECK_NONE:
            want = 0;
            break;
        case PINT_SERVER_CHECK_INVALID:
            js_p->error_code = -PVFS_EINVAL;
            goto cleanup;
    }
    js_p->error_code = PINT_check_acls(s_op->val.buffer,
                        s_op->val.read_sz,
                        obj_attr, 
                        s_op->req->credentials.uid,
                        s_op->req->credentials.gid,
                        want);
cleanup:
    gossip_debug(
        GOSSIP_PERMISSIONS_DEBUG, "Final permission check (after acls) \"%s\" set "
        "error code to %d (want %x)\n",
            PINT_map_server_op_to_string(s_op->req->op),
            js_p->error_code, want);

    if (s_op->val.buffer) 
        free(s_op->val.buffer);
    memset(&s_op->key, 0, sizeof(PVFS_ds_keyval));
    memset(&s_op->val, 0, sizeof(PVFS_ds_keyval));
    return SM_ACTION_COMPLETE;
}
