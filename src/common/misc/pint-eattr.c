/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "endecode-funcs.h"
#include "pvfs2.h"
#include "pint-eattr.h"
#include "pvfs2-req-proto.h"
#include "pvfs2-internal.h"

#define PVFS_EATTR_SYSTEM_NS "system.pvfs2."

/* This is used to encode/decode the datafile handles array when its retrieved
 * as an extended attribute (viewdist does this)
 */
struct PINT_handle_array
{
    int32_t count;
    PVFS_handle *handles;
};
endecode_fields_1a_struct(PINT_handle_array,
                          skip4,,
                          int32_t, count,
                          PVFS_handle, handles);

/* extended attribute name spaces supported in PVFS2 */
struct PINT_eattr_check
{
    const char * ns;
    int ret;
    int (* check) (PVFS_ds_keyval *key, PVFS_ds_keyval *val);
};

/* PINT_eattr_strip_prefix
 *
 * Remove the 'system.pvfs2.' prefix from attributes before querying.
 * These specific attributes are stored without the prefix.
 */
static int PINT_eattr_strip_prefix(PVFS_ds_keyval *key, PVFS_ds_keyval *val);

static struct PINT_eattr_check PINT_eattr_access[] =
{
    {PVFS_EATTR_SYSTEM_NS, 0, PINT_eattr_strip_prefix},
    {"system.", 0, NULL},
    {"user.", 0, NULL},
    {"trusted.", 0, NULL},
    {"security.", 0, NULL},
    {NULL, -PVFS_ENOENT, NULL}
};

/* PINT_eattr_system_verify
 *
 * Check the eattrs with system. namespace prefixes.  Right now this
 * only checks that acls are formatted properly.
 */
static int PINT_eattr_system_verify(PVFS_ds_keyval *k, PVFS_ds_keyval *v);

/* We provide namespace structures that include error codes
 * or checking functions.  For an extended attribute, the
 * namespace is checked by iterating from top to bottom
 * through this list.  If a namespace matches and the error
 * code is non-zero, the error code is returned, otherwise,
 * the checking function is called (if non-null).  If none of the namespaces
 * in the list match (or the eattr isn't prefixed with
 * a namespace, the last entry in the list is triggered,
 * and EOPNOTSUPP is returned.  Since we don't allow
 * eattrs with system.pvfs2. prefixes to be set by the
 * set-eattr operation, we return EINVAL in that case.
 */
static struct PINT_eattr_check PINT_eattr_namespaces[] =
{
    {PVFS_EATTR_SYSTEM_NS, -PVFS_EINVAL, NULL},
    {"system.", 0, PINT_eattr_system_verify},
    {"user.", 0, NULL},
    {"trusted.", 0, NULL},
    {"security.", 0, NULL},
    {NULL, -PVFS_EOPNOTSUPP, NULL}
};

static int PINT_eattr_verify_acl_access(PVFS_ds_keyval *key, PVFS_ds_keyval *val);

/* Used to verify that acls are correctly formatted before plopping
 * them in storage
 */
static struct PINT_eattr_check PINT_eattr_system[] =
{
    {"system.posix_acl_access", 0, PINT_eattr_verify_acl_access},
    {NULL, 0, NULL}};

static int PINT_eattr_add_pvfs_prefix(PVFS_ds_keyval *key, PVFS_ds_keyval *val);

/* check that the eattr in the list matches a valid namespace.  If
 * it doesn't, assume its in the system.pvfs2. namespace, and tack
 * on that prefix (using the PINT_eattr_add_pvfs_prefix function).
 */
static struct PINT_eattr_check PINT_eattr_list[] =
{
    {"system.", 0, NULL},
    {"user.", 0, NULL},
    {"trusted.", 0, NULL},
    {"security.", 0, NULL},
    {NULL, 0, PINT_eattr_add_pvfs_prefix}
};

static int PINT_eattr_encode_datafile_handle_array(
    PVFS_ds_keyval *key, PVFS_ds_keyval *val);

static struct PINT_eattr_check PINT_eattr_encode_keyvals[] =
{
    {DATAFILE_HANDLES_KEYSTR, 0,
     PINT_eattr_encode_datafile_handle_array},
    {NULL, 0, NULL}
};

static int PINT_eattr_decode_datafile_handle_array(
    PVFS_ds_keyval *key, PVFS_ds_keyval *val);

static struct PINT_eattr_check PINT_eattr_decode_keyvals[] =
{
    {PVFS_EATTR_SYSTEM_NS DATAFILE_HANDLES_KEYSTR, 0,
     PINT_eattr_decode_datafile_handle_array},
    {NULL, 0, NULL}
};

/* PINT_eattr_verify
 *
 * Useful as a basic function that does verification given an array of
 * extended attribute checking structures.  It essentially iterates
 * through the array and compares the key's buffer with each element.
 * If one matches, it uses the other fields in that checking structure
 * to verify the eattr.
 */
static inline int PINT_eattr_verify(
    struct PINT_eattr_check * eattr_array,
    PVFS_ds_keyval *key, PVFS_ds_keyval *val);

int PINT_eattr_check_access(PVFS_ds_keyval *key, PVFS_ds_keyval *val)
{
    return PINT_eattr_verify(PINT_eattr_access, key, val);
}

static int PINT_eattr_strip_prefix(PVFS_ds_keyval *key, PVFS_ds_keyval *val)
{
    char * tmp_buffer = NULL;
    int tmp_len;
    int ret;

    tmp_buffer = (char*)malloc(PVFS_REQ_LIMIT_KEY_LEN);
    if(!tmp_buffer)
    {
        return -PVFS_ENOMEM;
    }

    /* look for keys in the special "system.pvfs2." prefix and strip the
     * prefix off; they are stored as keyvals with no prefix within
     * trove.
     */
    ret = sscanf(key->buffer,
                 PVFS_EATTR_SYSTEM_NS "%s", tmp_buffer);
    if(ret != 1)
    {
        free(tmp_buffer);
        return -PVFS_ENOENT;
    }

    tmp_len = strlen(tmp_buffer) + 1;
    memcpy(key->buffer, tmp_buffer, tmp_len);
    key->buffer_sz = tmp_len;
    free(tmp_buffer);
    return 0;
}

static int PINT_eattr_encode_datafile_handle_array(
    PVFS_ds_keyval *key, PVFS_ds_keyval *val)
{
    char *tmp_buffer = NULL;
    char *ptr;
    struct PINT_handle_array harray;

    harray.count = val->read_sz / sizeof(PVFS_handle);
    harray.handles = (PVFS_handle *)val->buffer;

    /* allocate twice as much as we probably need here */
    tmp_buffer = malloc(harray.count * 2 * sizeof(int64_t));
    if(!tmp_buffer)
    {
        return -PVFS_ENOMEM;
    }

    ptr = tmp_buffer;
    encode_PINT_handle_array(&ptr, &harray);

    free(val->buffer);
    val->buffer = tmp_buffer;
    val->buffer_sz = val->read_sz = (ptr - tmp_buffer);
    return 0;
}

static int PINT_eattr_decode_datafile_handle_array(
    PVFS_ds_keyval *key, PVFS_ds_keyval *val)
{
    char *ptr;
    struct PINT_handle_array harray;

    ptr = val->buffer;
    decode_PINT_handle_array(&ptr, &harray);

    if(val->buffer_sz < (harray.count * sizeof(PVFS_handle)))
    {
        decode_free(harray.handles);
        return -PVFS_EMSGSIZE;
    }

    val->read_sz = val->buffer_sz = (harray.count * sizeof(PVFS_handle));
    memcpy(val->buffer, (void *)harray.handles, val->read_sz);

    return 0;
}

static inline int PINT_eattr_verify(
    struct PINT_eattr_check * eattr_array,
    PVFS_ds_keyval *key, PVFS_ds_keyval *val)
{
    int i = 0;
    while(1)
    {
        /* if we get to the NULL ns at the end of the list, return
         * the error code
         */
        if(!eattr_array[i].ns)
        {
            if(eattr_array[i].ret == 0 && eattr_array[i].check)
            {
                return eattr_array[i].check(key, val);
            }
            return eattr_array[i].ret;
        }

        if(strncmp(eattr_array[i].ns, key->buffer,
            strlen(eattr_array[i].ns)) == 0)
        {
            /* if the return value for this namespace is non-zero,
             * return that value
             */
            if(eattr_array[i].ret != 0)
            {
                return eattr_array[i].ret;
            }

            /* if the checking function for this namespace is non-null,
             * invoke that checking function and return the result.
             */
            if(eattr_array[i].check)
            {
                return eattr_array[i].check(key, val);
            }

            /* looks like the namespace is otherwise acceptable */
            return 0;
        }
        i++;
    }

    /* this shouldn't be possible */
    return(0);
}

int PINT_eattr_system_verify(PVFS_ds_keyval *k, PVFS_ds_keyval *v)
{
    return PINT_eattr_verify(PINT_eattr_system, k, v);
}

int PINT_eattr_namespace_verify(PVFS_ds_keyval *k, PVFS_ds_keyval *v)
{
    return PINT_eattr_verify(PINT_eattr_namespaces, k, v);
}

static int PINT_eattr_verify_acl_access(PVFS_ds_keyval *key, PVFS_ds_keyval *val)
{

    assert(!strcmp(key->buffer, "system.posix_acl_access"));

    /* verify that the acl is formatted properly.  Right now
     * all we can do is make sure the size matches a non-zero
     * number of pvfs acl entries.  The remainder should be 1
     * because the keyvals are padded with a null terminator
     */
    if(val->buffer_sz == 0 || 
       val->buffer_sz % sizeof(pvfs2_acl_entry) != 1)
    {
        return -PVFS_EINVAL;
    }

    return 0;
}

int PINT_eattr_list_access(PVFS_ds_keyval *key, PVFS_ds_keyval *val)
{
    return PINT_eattr_verify(PINT_eattr_list, key, val);
}

/* PINT_eattr_add_pvfs_prefix
 *
 * Tack on the system.pvfs2. prefix to any eattrs that don't have it.
 * This is used by the eattr list operation for pvfs specific attributes,
 * such as 'dh' (datafile handles) or 'md' (metafile distribution).
 */
static int PINT_eattr_add_pvfs_prefix(PVFS_ds_keyval *key, PVFS_ds_keyval *val)
{
    char * tmp_buffer = NULL;
    int ret;

    if(key->buffer_sz < (key->read_sz + sizeof(PVFS_EATTR_SYSTEM_NS)))
    {
        return -PVFS_EMSGSIZE;
    }

    tmp_buffer = (char*)malloc(key->read_sz + sizeof(PVFS_EATTR_SYSTEM_NS));
    if(!tmp_buffer)
    {
        return -PVFS_ENOMEM;
    }

    ret = sprintf(tmp_buffer, "%s%.*s",
                  PVFS_EATTR_SYSTEM_NS, key->read_sz, (char *)key->buffer);
    memcpy(key->buffer, tmp_buffer, ret+1);
    key->read_sz = ret+1;

    free(tmp_buffer);
    return 0;
}

int PINT_eattr_encode(PVFS_ds_keyval *key, PVFS_ds_keyval *val)
{
    return PINT_eattr_verify(PINT_eattr_encode_keyvals, key, val);
}

int PINT_eattr_decode(PVFS_ds_keyval *key, PVFS_ds_keyval *val)
{
    return PINT_eattr_verify(PINT_eattr_decode_keyvals, key, val);
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
