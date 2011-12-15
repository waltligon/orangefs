/* 
 * (C) 2009 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "pvfs2-config.h"
#include "pvfs2-types.h"
#include "pvfs2-debug.h"
#include "pvfs2-internal.h"
#include "gossip.h"
#include "pint-util.h"
#include "security-util.h"

/* PINT_print_op_mask
 *
 * Writes capability operation mask into string buffer.
 * Each operation has a code:
 *    m - BATCH_REMOVE
 *    e - BATCH_CREATE
 *    v - REMOVE
 *    a - ADMIN
 *    c - CREATE
 *    s - SETATTR
 *    r - READ
 *    w - WRITE
 *    x - EXECUTE
 *
 * A - is printed in place of a code if it is not set.
 * The buffer must be at least 10 bytes.
 * Returns pointer to the buffer.
 */
char *PINT_print_op_mask(uint32_t op_mask, char *out_buf)
{
    const char codes[] = "mevacsrwx";
    uint32_t bit, i;
    char *p;

    if (!out_buf)
    {
        return NULL;
    }

    /* start from "left" */
    for (bit = (1 << 8), i = 0, p = out_buf;
         bit; 
         bit >>= 1, i++, p++)
    {
        if (op_mask & bit)
        {
            snprintf(p, 2, "%c", codes[i]);
        }
        else
        {
            snprintf(p, 2, "-");
        }
    }

    return out_buf;
}

/* PINT_null_capability
 *
 * Creates a capability object with no permissions.
 */
void PINT_null_capability(PVFS_capability *cap)
{
    memset(cap, 0, sizeof(PVFS_capability));
    cap->issuer = strdup("");
}

/* PINT_capability_is_null
 *
 * Checks for a null capability with no permissions.
 *
 * returns 1 if the capability is null
 * returns 0 if the capability is not null
 */
int PINT_capability_is_null(const PVFS_capability *cap)
{
    int ret;

    ret = (!strcmp(cap->issuer, "")) && (cap->op_mask == 0);

    return ret;
}

/* PINT_dup_capability
 *
 * Duplicates a capability object by allocating memory for the
 * new object and then performing a deep copy.
 *
 * returns the new capability object on success
 * returns NULL on error
 */
PVFS_capability *PINT_dup_capability(const PVFS_capability *cap)
{
    PVFS_capability *newcap;
    int ret;
    
    if (!cap)
    {
        return NULL;
    }

    newcap = malloc(sizeof(PVFS_capability));
    if (!newcap)
    {
        return NULL;
    }

    ret = PINT_copy_capability(cap, newcap);
    if (ret < 0)
    {
        free(newcap);
        newcap = NULL;
    }

    return newcap;
}

/* PINT_copy_capability
 *
 * Performs a deep copy of a capability object.
 *
 * returns 0 on success
 * returns negative PVFS error code on failure
 */
int PINT_copy_capability(const PVFS_capability *src, PVFS_capability *dest)
{
    if (!src || !dest || (src == dest))
    {
        return -PVFS_EINVAL;
    }

    /* first copy by value */
    memcpy(dest, src, sizeof(PVFS_capability));
    dest->issuer = NULL;
    dest->signature = NULL;
    dest->handle_array = NULL;

    dest->issuer = strdup(src->issuer);
    if (!dest->issuer)
    {
        return -PVFS_ENOMEM;
    }

    if (src->sig_size)
    {
        dest->signature = malloc(src->sig_size);
        if (!dest->signature)
        {
            free(dest->issuer);
            return -PVFS_ENOMEM;
        }
        memcpy(dest->signature, src->signature, src->sig_size);
    }

    if (src->num_handles)
    {
        dest->handle_array = calloc(src->num_handles, sizeof(PVFS_handle));
        if (!dest->handle_array)
        {
            free(dest->signature);
            free(dest->issuer);
            return -PVFS_ENOMEM;
        }
        memcpy(dest->handle_array, src->handle_array,
               src->num_handles * sizeof(PVFS_handle));
    }

    return 0;
}

/* PINT_debug_capability                                                             
 * 
 * Outputs the fields of a capability.
 * prefix should typically be "Received" or "Created".      
 */
void PINT_debug_capability(const PVFS_capability *cap, const char *prefix)
{
    char sig_buf[10], mask_buf[10]; 

    assert(cap);

    if (strlen(cap->issuer) == 0)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s null capability\n", prefix);
        return;
    }

    gossip_debug(GOSSIP_SECURITY_DEBUG, "%s capability:\n", prefix);
    gossip_debug(GOSSIP_SECURITY_DEBUG, "\tissuer: %s\n", cap->issuer);
    gossip_debug(GOSSIP_SECURITY_DEBUG, "\tfsid: %u\n", cap->fsid);
    gossip_debug(GOSSIP_SECURITY_DEBUG, "\tsig_size: %u\n", cap->sig_size);
    gossip_debug(GOSSIP_SECURITY_DEBUG, "\tsignature: %s\n",
                 PINT_util_bytes2str(cap->signature, sig_buf, 4));
    gossip_debug(GOSSIP_SECURITY_DEBUG, "\ttimeout: %d\n",
                 (int) cap->timeout);
    gossip_debug(GOSSIP_SECURITY_DEBUG, "\top_mask: %s\n",
                 PINT_print_op_mask(cap->op_mask, mask_buf));
    gossip_debug(GOSSIP_SECURITY_DEBUG, "\tnum_handles: %u\n", 
                 cap->num_handles);
    gossip_debug(GOSSIP_SECURITY_DEBUG, "\tfirst handle: %llu\n",
                 cap->num_handles > 0 ? llu(cap->handle_array[0]) : 0LL);

}

/* PINT_cleanup_capability
 *
 * Destructs a capability object by freeing its internal structures.
 * After this function returns the capability object is in an
 * invalid state.
 */
void PINT_cleanup_capability(PVFS_capability *cap)
{
    if (cap)
    {
        if (cap->handle_array)
        {
            free(cap->handle_array);
        }
        if (cap->signature)
        {
            free(cap->signature);
        }
        if (cap->issuer)
        {
            free(cap->issuer);
        }
        
        cap->handle_array = NULL;
        cap->signature = NULL;
        cap->sig_size = 0;
        cap->issuer = NULL;
    }
}

/* PINT_dup_credential
 *
 * Duplicates a credential object by allocating memory for the
 * new object and then performing a deep copy.
 *
 * returns the new credential object on success
 * returns NULL on error
 */
PVFS_credential *PINT_dup_credential(const PVFS_credential *cred)
{
    PVFS_credential *newcred;
    int ret;

    if (!cred)
    {
        return NULL;
    }

    newcred = malloc(sizeof(PVFS_credential));
    if (!newcred)
    {
        return NULL;
    }

    ret = PINT_copy_credential(cred, newcred);
    if (ret < 0)
    {
        free(newcred);
        return NULL;
    }

    return newcred;
}

/* PINT_copy_credential
 *
 * Performs a deep copy of a credential object.
 *
 * returns 0 on success
 * returns negative PVFS error code on failure
 */
int PINT_copy_credential(const PVFS_credential *src, PVFS_credential *dest)
{
    if (!src || !dest || (src == dest))
    {
        return -PVFS_EINVAL;
    }

    /* first copy by value */
    memcpy(dest, src, sizeof(PVFS_credential));
    dest->issuer = NULL;
    dest->signature = NULL;
    dest->group_array = NULL;

    dest->issuer = strdup(src->issuer);
    if (!dest->issuer)
    {
        return -PVFS_ENOMEM;
    }

    if (src->sig_size)
    {
        dest->signature = malloc(src->sig_size);
        if (!dest->signature)
        {
            free(dest->issuer);
            return -PVFS_ENOMEM;
        }
        memcpy(dest->signature, src->signature, src->sig_size);
    }

    if (src->num_groups)
    {
        dest->group_array = calloc(src->num_groups, sizeof(PVFS_gid));
        if (!dest->group_array)
        {
            free(dest->signature);
            free(dest->issuer);
            return -PVFS_ENOMEM;
        }
        memcpy(dest->group_array, src->group_array,
               src->num_groups * sizeof(PVFS_gid));
    }
    
    return 0;
}

/* PINT_debug_credential
 * 
 * Outputs the fields of a credential.
 * Set prefix to descriptive text.
 */
void PINT_debug_credential(const PVFS_credential *cred, const char *prefix)
{
    char sig_buf[10], group_buf[512], temp_buf[16];
    unsigned int i, buf_left, count;

    assert(cred);

    gossip_debug(GOSSIP_SECURITY_DEBUG, "%s:\n", prefix);

    gossip_debug(GOSSIP_SECURITY_DEBUG, "\tissuer: %s\n", cred->issuer);
    gossip_debug(GOSSIP_SECURITY_DEBUG, "\tuserid: %u\n", cred->userid);
    /* output groups */
    for (i = 0, group_buf[0] = '\0', buf_left = 512; i < cred->num_groups; i++)
    {
        count = sprintf(temp_buf, "%u ", cred->group_array[i]);
        if (count > buf_left)
        {
            break;
        }
        strcat(group_buf, temp_buf);
        buf_left -= count;
    }
    gossip_debug(GOSSIP_SECURITY_DEBUG, "\tgroups: %s\n", group_buf);
    gossip_debug(GOSSIP_SECURITY_DEBUG, "\tsig_size: %u\n", cred->sig_size);
    gossip_debug(GOSSIP_SECURITY_DEBUG, "\tsignature: %s\n",
                 PINT_util_bytes2str(cred->signature, sig_buf, 4));
    gossip_debug(GOSSIP_SECURITY_DEBUG, "\ttimeout: %d\n", 
                 (int) cred->timeout);

}

/* PINT_cleanup_credential
 *
 * Destructs a credential object by freeing its internal structures.
 * After this function returns the credential object is in an
 * invalid state.
 */
void PINT_cleanup_credential(PVFS_credential *cred)
{
    if (cred)
    {
        if (cred->group_array)
        {
            free(cred->group_array);
        }
        if (cred->issuer)
        {
            free(cred->issuer);
        }
        if (cred->signature)
        {
            free(cred->signature);
        }
        
        cred->group_array = NULL;
        cred->issuer = NULL;
        cred->signature = NULL;
        cred->sig_size = 0;
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
