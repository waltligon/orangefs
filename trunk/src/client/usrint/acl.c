/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
/** \file
 *  \ingroup usrint
 *
 *  ACL Operations for user interface
 *
 *  Everything here is done through calls to the posix-like
 *  xattr interface.  There are no calls directly to iocommon.
 */

#include "usrint.h"
#include "posix-pvfs.h"

/* taken from src/kernel/linux2.6 */
#define PVFS2_XATTR_INDEX_POSIX_ACL_ACCESS  1
#define PVFS2_XATTR_INDEX_POSIX_ACL_DEFAULT 2
#define PVFS2_XATTR_INDEX_TRUSTED           3
#define PVFS2_XATTR_INDEX_DEFAULT           4

#ifndef POSIX_ACL_XATTR_ACCESS
#define POSIX_ACL_XATTR_ACCESS  "system.posix_acl_access"
#endif

#ifndef POSIX_ACL_XATTR_DEFAULT
#define POSIX_ACL_XATTR_DEFAULT "system.posix_acl_default"
#endif

#define PVFS2_XATTR_NAME_ACL_ACCESS  POSIX_ACL_XATTR_ACCESS
#define PVFS2_XATTR_NAME_ACL_DEFAULT POSIX_ACL_XATTR_DEFAULT
#define PVFS2_XATTR_NAME_TRUSTED_PREFIX "trusted."
#define PVFS2_XATTR_NAME_DEFAULT_PREFIX ""

/** PVFS acl_delete_def_file
 *
 */
int pvfs_acl_delete_def_file(const char *path_p)
{
    int rc;
    struct stat sbuf;

    rc = pvfs_stat(path_p, &sbuf);
    if (rc < 0)
    {
        return -1;
    }
    if (!S_ISDIR(sbuf.st_mode))
    {
        errno = ENOTDIR;
        return -1;
    }
    rc = pvfs_removexattr(path_p, POSIX_ACL_XATTR_DEFAULT);
    return rc;
}

/** PVFS acl_get_fd
 *
 */
acl_t pvfs_acl_get_fd(int fd)
{
    int rc, i;
    int bufsize = sizeof(pvfs2_acl_entry) * 100;
    pvfs2_acl_entry *pvfs_entry = (pvfs2_acl_entry *)malloc(bufsize);
    int count;
    acl_t acl;

    rc = pvfs_fgetxattr(fd, POSIX_ACL_XATTR_ACCESS, pvfs_entry, bufsize);
    if (rc < 0)
    {
        return NULL;
    }
    if (rc > bufsize)
    {
        free(pvfs_entry);
        pvfs_entry = (pvfs2_acl_entry *)malloc(rc);
        rc = pvfs_fgetxattr(fd, POSIX_ACL_XATTR_ACCESS, pvfs_entry, rc);
        /* should not error */
    }
    count = rc / sizeof(pvfs2_acl_entry);
    acl = acl_init(count);
    for (i = 0; i < count; i++)
    {
        acl_entry_t entry;
        acl_tag_t tag;
        int qual;
        acl_permset_t permset;

        rc = acl_create_entry(&acl, &entry);
        if (rc < 0)
        {
            return NULL;
        }
        switch(pvfs_entry[i].p_tag)
        {
        case PVFS2_ACL_USER_OBJ :
            tag = ACL_USER_OBJ;
            break;
        case PVFS2_ACL_USER :
            tag = ACL_USER;
            break;
        case PVFS2_ACL_GROUP_OBJ :
            tag = ACL_GROUP_OBJ;
            break;
        case PVFS2_ACL_GROUP :
            tag = ACL_GROUP;
            break;
        case PVFS2_ACL_MASK :
            tag = ACL_MASK;
            break;
        case PVFS2_ACL_OTHER :
            tag = ACL_OTHER;
            break;
        default:
            errno = EINVAL;
            return NULL;
        }
        rc = acl_set_tag_type(entry, tag);
        qual = pvfs_entry[i].p_id;
        rc = acl_set_qualifier(entry, (const void *)&qual);
        if (rc < 0)
        {
            return NULL;
        }
        rc = acl_get_permset(entry, &permset);
        if (rc < 0)
        {
            return NULL;
        }
        acl_clear_perms(permset);
        if (pvfs_entry[i].p_perm & PVFS2_ACL_READ);
        {
            acl_add_perm(permset, ACL_READ);
        }
        if (pvfs_entry[i].p_perm & PVFS2_ACL_WRITE);
        {
            acl_add_perm(permset, ACL_WRITE);
        }
        if (pvfs_entry[i].p_perm & PVFS2_ACL_EXECUTE);
        {
            acl_add_perm(permset, ACL_EXECUTE);
        }
        rc = acl_set_permset(entry, permset);
        if (rc < 0)
        {
            return NULL;
        }
    }
    return acl;
}

/** PVFS acl_get_file
 *
 */
acl_t pvfs_acl_get_file(const char *path_p, acl_type_t type)
{
    int rc, i;
    int bufsize = sizeof(pvfs2_acl_entry) * 100;
    pvfs2_acl_entry *pvfs_entry = (pvfs2_acl_entry *)malloc(bufsize);
    int count;
    acl_t acl;

    switch (type)
    {
    case ACL_TYPE_ACCESS :
        rc = pvfs_getxattr(path_p, POSIX_ACL_XATTR_ACCESS,
                            pvfs_entry, bufsize);
        break;
    case ACL_TYPE_DEFAULT :
        rc = pvfs_getxattr(path_p, POSIX_ACL_XATTR_DEFAULT,
                            pvfs_entry, bufsize);
        break;
    default:
        errno = EINVAL;
        return NULL;
    }
    if (rc < 0)
    {
        return NULL;
    }
    if (rc > bufsize)
    {
        free(pvfs_entry);
        pvfs_entry = (pvfs2_acl_entry *)malloc(rc);
        switch (type)
        {
        case ACL_TYPE_ACCESS :
            rc = pvfs_getxattr(path_p, POSIX_ACL_XATTR_ACCESS,
                                pvfs_entry, rc);
            break;
        case ACL_TYPE_DEFAULT :
            rc = pvfs_getxattr(path_p, POSIX_ACL_XATTR_DEFAULT,
                                pvfs_entry, rc);
            break;
        default:
            errno = EINVAL;
            return NULL;
        }
        /* should not error */
    }
    count = rc / sizeof(pvfs2_acl_entry);
    acl = acl_init(count);
    for (i = 0; i < count; i++)
    {
        acl_entry_t entry;
        acl_tag_t tag;
        int qual;
        acl_permset_t permset;

        rc = acl_create_entry(&acl, &entry);
        if (rc < 0)
        {
            return NULL;
        }
        switch(pvfs_entry[i].p_tag)
        {
        case PVFS2_ACL_USER_OBJ :
            tag = ACL_USER_OBJ;
            break;
        case PVFS2_ACL_USER :
            tag = ACL_USER;
            break;
        case PVFS2_ACL_GROUP_OBJ :
            tag = ACL_GROUP_OBJ;
            break;
        case PVFS2_ACL_GROUP :
            tag = ACL_GROUP;
            break;
        case PVFS2_ACL_MASK :
            tag = ACL_MASK;
            break;
        case PVFS2_ACL_OTHER :
            tag = ACL_OTHER;
            break;
        default:
            errno = EINVAL;
            return NULL;
        }
        rc = acl_set_tag_type(entry, tag);
        if (rc < 0)
        {
            return NULL;
        }
        qual = pvfs_entry[i].p_id;
        rc = acl_set_qualifier(entry, (const void *)&qual);
        if (rc < 0)
        {
            return NULL;
        }
        rc = acl_get_permset(entry, &permset);
        if (rc < 0)
        {
            return NULL;
        }
        acl_clear_perms(permset);
        if (pvfs_entry[i].p_perm & PVFS2_ACL_READ);
        {
            acl_add_perm(permset, ACL_READ);
        }
        if (pvfs_entry[i].p_perm & PVFS2_ACL_WRITE);
        {
            acl_add_perm(permset, ACL_WRITE);
        }
        if (pvfs_entry[i].p_perm & PVFS2_ACL_EXECUTE);
        {
            acl_add_perm(permset, ACL_EXECUTE);
        }
        rc = acl_set_permset(entry, permset);
        if (rc < 0)
        {
            return NULL;
        }
    }
    return acl;
}

/** PVFS acl_set_fd
 *
 */
int pvfs_acl_set_fd(int fd, acl_t acl)
{
    int count = 0;
    int rc = 0;
    int i;
    acl_entry_t entry;
    pvfs2_acl_entry *pvfs_entry;

    if (!acl_valid(acl))
    {
        errno = EINVAL;
        return -1;
    }
    rc = acl_get_entry(acl, ACL_FIRST_ENTRY, &entry);
    if (rc == -1)
    {
        return -1;
    }
    while(rc == 1)
    {
        count++;
        rc = acl_get_entry(acl, ACL_NEXT_ENTRY, &entry);
    }
    if (rc == -1)
    {
        return -1;
    }
    pvfs_entry = (pvfs2_acl_entry *)malloc(sizeof(pvfs2_acl_entry) * count);
    if (!pvfs_entry)
    {
        return -1;
    }
    rc = acl_get_entry(acl, ACL_FIRST_ENTRY, &entry);
    if (rc == -1)
    {
        free(pvfs_entry);
        return -1;
    }
    for (i = 0; i < count; i++)
    {
        acl_tag_t tag;
        void *p;
        acl_permset_t permset;

        rc = acl_get_tag_type(entry, &tag);
        if (rc == -1)
        {
            free(pvfs_entry);
            return -1;
        }
        p = acl_get_qualifier(entry);
        if (!p)
        {
            free(pvfs_entry);
            return -1;
        }
        rc = acl_get_permset(entry, &permset);
        if (rc == -1)
        {
            free(pvfs_entry);
            return -1;
        }
        /* should probably translate these values */
        switch (tag)
        {
            case ACL_USER_OBJ :
                pvfs_entry[i].p_tag = PVFS2_ACL_USER_OBJ;
                break;
            case ACL_USER :
                pvfs_entry[i].p_tag = PVFS2_ACL_USER;
                break;
            case ACL_GROUP_OBJ :
                pvfs_entry[i].p_tag = PVFS2_ACL_GROUP_OBJ;
                break;
            case ACL_GROUP :
                pvfs_entry[i].p_tag = PVFS2_ACL_GROUP;
                break;
            case ACL_MASK :
                pvfs_entry[i].p_tag = PVFS2_ACL_MASK;
                break;
            case ACL_OTHER :
                pvfs_entry[i].p_tag = PVFS2_ACL_OTHER;
                break;
            default:
                errno = EINVAL;
                return -1;
        }
        acl_get_permset(entry, &permset);
        if (acl_get_perm(permset, ACL_READ))
        {
            pvfs_entry[i].p_perm |= PVFS2_ACL_READ;
        }
        if (acl_get_perm(permset, ACL_WRITE))
        {
            pvfs_entry[i].p_perm |= PVFS2_ACL_WRITE;
        }
        if (acl_get_perm(permset, ACL_EXECUTE))
        {
            pvfs_entry[i].p_perm |= PVFS2_ACL_EXECUTE;
        }
        pvfs_entry[i].p_id = *(uint32_t *)p;
        free(p);
        rc = acl_get_entry(acl, ACL_NEXT_ENTRY, &entry);
        if (rc == -1)
        {
            free(pvfs_entry);
            return -1;
        }
    }
    rc = pvfs_fsetxattr(fd, POSIX_ACL_XATTR_ACCESS, pvfs_entry,
                        sizeof(pvfs2_acl_entry) * count, 0);
    free(pvfs_entry);
    return rc;
}

/** PVFS acl_set_file
 *
 */
int pvfs_acl_set_file(const char *path_p, acl_type_t type, acl_t acl)
{
    int count = 0;
    int rc = 0;
    int i;
    acl_entry_t entry;
    pvfs2_acl_entry *pvfs_entry;

    if (!acl_valid(acl))
    {
        errno = EINVAL;
        return -1;
    }
    rc = acl_get_entry(acl, ACL_FIRST_ENTRY, &entry);
    if (rc == -1)
    {
        return -1;
    }
    while(rc == 1)
    {
        count++;
        rc = acl_get_entry(acl, ACL_NEXT_ENTRY, &entry);
    }
    if (rc == -1)
    {
        return -1;
    }
    pvfs_entry = (pvfs2_acl_entry *)malloc(sizeof(pvfs2_acl_entry) * count);
    if (!pvfs_entry)
    {
        return -1;
    }
    rc = acl_get_entry(acl, ACL_FIRST_ENTRY, &entry);
    if (rc == -1)
    {
        free(pvfs_entry);
        return -1;
    }
    for (i = 0; i < count; i++)
    {
        acl_tag_t tag;
        void *p;
        acl_permset_t permset;

        rc = acl_get_tag_type(entry, &tag);
        if (rc == -1)
        {
            free(pvfs_entry);
            return -1;
        }
        p = acl_get_qualifier(entry);
        if (!p)
        {
            free(pvfs_entry);
            return -1;
        }
        rc = acl_get_permset(entry, &permset);
        if (rc == -1)
        {
            free(pvfs_entry);
            return -1;
        }
        /* should probably translate these values */
        switch (tag)
        {
            case ACL_USER_OBJ :
                pvfs_entry[i].p_tag = PVFS2_ACL_USER_OBJ;
                break;
            case ACL_USER :
                pvfs_entry[i].p_tag = PVFS2_ACL_USER;
                break;
            case ACL_GROUP_OBJ :
                pvfs_entry[i].p_tag = PVFS2_ACL_GROUP_OBJ;
                break;
            case ACL_GROUP :
                pvfs_entry[i].p_tag = PVFS2_ACL_GROUP;
                break;
            case ACL_MASK :
                pvfs_entry[i].p_tag = PVFS2_ACL_MASK;
                break;
            case ACL_OTHER :
                pvfs_entry[i].p_tag = PVFS2_ACL_OTHER;
                break;
            default:
                errno = EINVAL;
                return -1;
        }
        acl_get_permset(entry, &permset);
        if (acl_get_perm(permset, ACL_READ))
        {
            pvfs_entry[i].p_perm |= PVFS2_ACL_READ;
        }
        if (acl_get_perm(permset, ACL_WRITE))
        {
            pvfs_entry[i].p_perm |= PVFS2_ACL_WRITE;
        }
        if (acl_get_perm(permset, ACL_EXECUTE))
        {
            pvfs_entry[i].p_perm |= PVFS2_ACL_EXECUTE;
        }
        pvfs_entry[i].p_id = *(uint32_t *)p;
        free(p);
        rc = acl_get_entry(acl, ACL_NEXT_ENTRY, &entry);
        if (rc == -1)
        {
            free(pvfs_entry);
            return -1;
        }
    }
    switch (type)
    {
    case ACL_TYPE_ACCESS :
        rc = pvfs_setxattr(path_p, POSIX_ACL_XATTR_ACCESS, pvfs_entry,
                            sizeof(pvfs2_acl_entry) * count, 0);
        break;
    case ACL_TYPE_DEFAULT :
        rc = pvfs_setxattr(path_p, POSIX_ACL_XATTR_DEFAULT, pvfs_entry,
                            sizeof(pvfs2_acl_entry) * count, 0);
        break;
    default:
        errno = EINVAL;
        rc = -1;
        break;
    }
    free(pvfs_entry);
    return rc;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

