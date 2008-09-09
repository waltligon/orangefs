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

/* This file includes definitions of common internal utility functions */
#include <string.h>
#include <assert.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "gen-locks.h"
#include "pint-util.h"
#include "bmi.h"
#include "gossip.h"
#include "pvfs2-req-proto.h"

void PINT_time_mark(PINT_time_marker *out_marker)
{
    struct rusage usage;

    gettimeofday(&out_marker->wtime, NULL);
    getrusage(RUSAGE_SELF, &usage);
    out_marker->utime = usage.ru_utime;
    out_marker->stime = usage.ru_stime;
}

void PINT_time_diff(PINT_time_marker mark1, 
                    PINT_time_marker mark2,
                    double *out_wtime_sec,
                    double *out_utime_sec,
                    double *out_stime_sec)
{
    *out_wtime_sec = 
        ((double)mark2.wtime.tv_sec +
         (double)(mark2.wtime.tv_usec) / 1000000) -
        ((double)mark1.wtime.tv_sec +
         (double)(mark1.wtime.tv_usec) / 1000000);

    *out_stime_sec = 
        ((double)mark2.stime.tv_sec +
         (double)(mark2.stime.tv_usec) / 1000000) -
        ((double)mark1.stime.tv_sec +
         (double)(mark1.stime.tv_usec) / 1000000);

    *out_utime_sec = 
        ((double)mark2.utime.tv_sec +
         (double)(mark2.utime.tv_usec) / 1000000) -
        ((double)mark1.utime.tv_sec +
         (double)(mark1.utime.tv_usec) / 1000000);
}

static int current_tag = 1;
static gen_mutex_t current_tag_lock = GEN_MUTEX_INITIALIZER;

PVFS_msg_tag_t PINT_util_get_next_tag(void)
{
    PVFS_msg_tag_t ret;

    gen_mutex_lock(&current_tag_lock);
    ret = current_tag;

    /* increment the tag, don't use zero */
    if (current_tag + 1 == PINT_MSG_TAG_INVALID)
    {
	current_tag = 1;
    }
    else
    {
	current_tag++;
    }
    gen_mutex_unlock(&current_tag_lock);

    return ret;
}

int PINT_copy_object_attr(PVFS_object_attr *dest, PVFS_object_attr *src)
{
    int ret = -PVFS_ENOMEM;

    if (dest && src)
    {
	if (src->mask & PVFS_ATTR_COMMON_UID)
        {
            dest->owner = src->owner;
        }
	if (src->mask & PVFS_ATTR_COMMON_GID)
        {
            dest->group = src->group;
        }
	if (src->mask & PVFS_ATTR_COMMON_PERM)
        {
            dest->perms = src->perms;
        }
	if (src->mask & PVFS_ATTR_COMMON_ATIME)
        {
            dest->atime = src->atime;
        }
	if (src->mask & PVFS_ATTR_COMMON_CTIME)
        {
            dest->ctime = src->ctime;
        }
        if (src->mask & PVFS_ATTR_COMMON_MTIME)
        {
            dest->mtime = src->mtime;
        }
	if (src->mask & PVFS_ATTR_COMMON_TYPE)
        {
            dest->objtype = src->objtype;
        }

        if (src->mask & PVFS_ATTR_DIR_DIRENT_COUNT)
        {
            dest->u.dir.dirent_count = 
                src->u.dir.dirent_count;
        }

        if((src->objtype == PVFS_TYPE_METAFILE) &&
            (!(src->mask & PVFS_ATTR_META_UNSTUFFED)))
        {
            /* if this is a metafile, and does _not_ appear to be stuffed,
             * then we should propigate the stuffed_size
             */
            dest->u.meta.stuffed_size = 
                src->u.meta.stuffed_size;
        }

        if (src->mask & PVFS_ATTR_DIR_HINT)
        {
            dest->u.dir.hint.dfile_count = 
                src->u.dir.hint.dfile_count;
            dest->u.dir.hint.dist_name_len =
                src->u.dir.hint.dist_name_len;
            if (dest->u.dir.hint.dist_name_len > 0)
            {
                dest->u.dir.hint.dist_name = strdup(src->u.dir.hint.dist_name);
                if (dest->u.dir.hint.dist_name == NULL)
                {
                    return ret;
                }
            }
            dest->u.dir.hint.dist_params_len =
                src->u.dir.hint.dist_params_len;
            if (dest->u.dir.hint.dist_params_len > 0)
            {
                dest->u.dir.hint.dist_params = strdup(src->u.dir.hint.dist_params);
                if (dest->u.dir.hint.dist_params == NULL)
                {
                    free(dest->u.dir.hint.dist_name);
                    return ret;
                }
            }
        }

        /*
          NOTE:
          we only copy the size out if we're actually a
          datafile object.  sometimes the size field is
          valid when the objtype is a metafile because
          of different uses of the acache.  In this case
          (namely, getattr), the size is stored in the
          acache before this deep copy, so it's okay
          that we're not copying here even though the
          size mask bit is set.

          if we don't do this trick, the metafile that
          caches the size will have it's union data
          overwritten with a bunk size.
        */
        if ((src->mask & PVFS_ATTR_DATA_SIZE) &&
            (src->mask & PVFS_ATTR_COMMON_TYPE) &&
            (src->objtype == PVFS_TYPE_DATAFILE))
        {
            dest->u.data.size = src->u.data.size;
        }

	if ((src->mask & PVFS_ATTR_COMMON_TYPE) &&
            (src->objtype == PVFS_TYPE_METAFILE))
        {      
            if(src->mask & PVFS_ATTR_META_DFILES)
            {
                PVFS_size df_array_size = src->u.meta.dfile_count *
                    sizeof(PVFS_handle);

                if (df_array_size)
                {
                    if ((dest->mask & PVFS_ATTR_META_DFILES) &&
                        dest->u.meta.dfile_count > 0)
                    {
                        if (dest->u.meta.dfile_array)
                        {
                            free(dest->u.meta.dfile_array);
                            dest->u.meta.dfile_array = NULL;
                        }
                    }
                    dest->u.meta.dfile_array = malloc(df_array_size);
                    if (!dest->u.meta.dfile_array)
                    {
                        return ret;
                    }
                    memcpy(dest->u.meta.dfile_array,
                           src->u.meta.dfile_array, df_array_size);
                }
                else
                {
                    dest->u.meta.dfile_array = NULL;
                }
                dest->u.meta.dfile_count = src->u.meta.dfile_count;
            }

            if(src->mask & PVFS_ATTR_META_DIST)
            {
                assert(src->u.meta.dist_size > 0);

                if ((dest->mask & PVFS_ATTR_META_DIST) && dest->u.meta.dist)
                {
                    PINT_dist_free(dest->u.meta.dist);
                }
                dest->u.meta.dist = PINT_dist_copy(src->u.meta.dist);
                if (dest->u.meta.dist == NULL)
                {
                    return ret;
                }
                dest->u.meta.dist_size = src->u.meta.dist_size;
            }
            memcpy(&dest->u.meta.hint, &src->u.meta.hint, sizeof(dest->u.meta.hint));
        }

        if (src->mask & PVFS_ATTR_SYMLNK_TARGET)
        {
            dest->u.sym.target_path_len = src->u.sym.target_path_len;
            dest->u.sym.target_path = strdup(src->u.sym.target_path);
            if (dest->u.sym.target_path == NULL)
            {
                return ret;
            }
        }

	dest->mask = src->mask;
        ret = 0;
    }
    return ret;
}

void PINT_free_object_attr(PVFS_object_attr *attr)
{
    if (attr)
    {
        if (attr->objtype == PVFS_TYPE_METAFILE)
        {
            if (attr->mask & PVFS_ATTR_META_DFILES)
            {
                if (attr->u.meta.dfile_array)
                {
                    free(attr->u.meta.dfile_array);
                    attr->u.meta.dfile_array = NULL;
                }
            }
            if (attr->mask & PVFS_ATTR_META_DIST)
            {
                if (attr->u.meta.dist)
                {
                    PINT_dist_free(attr->u.meta.dist);
                    attr->u.meta.dist = NULL;
                }
            }
        }
        else if (attr->objtype == PVFS_TYPE_SYMLINK)
        {
            if (attr->mask & PVFS_ATTR_SYMLNK_TARGET)
            {
                if ((attr->u.sym.target_path_len > 0) &&
                    attr->u.sym.target_path)
                {
                    free(attr->u.sym.target_path);
                    attr->u.sym.target_path = NULL;
                }
            }
        }
        else if (attr->objtype == PVFS_TYPE_DIRECTORY)
        {
            if ((attr->mask & PVFS_ATTR_DIR_HINT) || (attr->mask & PVFS_ATTR_DIR_DIRENT_COUNT))
            {
                if (attr->u.dir.hint.dist_name)
                {
                    free(attr->u.dir.hint.dist_name);
                    attr->u.dir.hint.dist_name = NULL;
                }
                if (attr->u.dir.hint.dist_params)
                {
                    free(attr->u.dir.hint.dist_params);
                    attr->u.dir.hint.dist_params = NULL;
                }
            }
        }
    }
}

char *PINT_util_get_object_type(int objtype)
{
    static char *obj_types[] =
    {
         "NONE", "METAFILE", "DATAFILE",
         "DIRECTORY", "SYMLINK", "DIRDATA", "UNKNOWN"
    };
    switch(objtype)
    {
    case PVFS_TYPE_NONE:
         return obj_types[0];
    case PVFS_TYPE_METAFILE:
         return obj_types[1];
    case PVFS_TYPE_DATAFILE:
         return obj_types[2];
    case PVFS_TYPE_DIRECTORY:
         return obj_types[3];
    case PVFS_TYPE_SYMLINK:
         return obj_types[4];
    case PVFS_TYPE_DIRDATA:
         return obj_types[5];
    }
    return obj_types[6];
}

void PINT_util_get_current_timeval(struct timeval *tv)
{
    gettimeofday(tv, NULL);
}

int PINT_util_get_timeval_diff(struct timeval *tv_start, struct timeval *tv_end)
{
    return (tv_end->tv_sec * 1e6 + tv_end->tv_usec) -
        (tv_start->tv_sec * 1e6 + tv_start->tv_usec);
}


PVFS_time PINT_util_get_current_time(void)
{
    struct timeval t = {0,0};
    PVFS_time current_time = 0;

    gettimeofday(&t, NULL);
    current_time = (PVFS_time)t.tv_sec;
    return current_time;
}

PVFS_time PINT_util_mktime_version(PVFS_time time)
{
    struct timeval t = {0,0};
    PVFS_time version = (time << 32);

    gettimeofday(&t, NULL);
    version |= (PVFS_time)t.tv_usec;
    return version;
}

PVFS_time PINT_util_mkversion_time(PVFS_time version)
{
    return (PVFS_time)(version >> 32);
}

struct timespec PINT_util_get_abs_timespec(int microsecs)
{
    struct timeval now, add, result;
    struct timespec tv;

    gettimeofday(&now, NULL);
    add.tv_sec = (microsecs / 1e6);
    add.tv_usec = (microsecs % 1000000);
    timeradd(&now, &add, &result);
    tv.tv_sec = result.tv_sec;
    tv.tv_nsec = result.tv_usec * 1e3;
    return tv;
}

void PINT_util_gen_credentials(
    PVFS_credentials *credentials)
{
    assert(credentials);

    memset(credentials, 0, sizeof(PVFS_credentials));
    credentials->uid = geteuid();
    credentials->gid = getegid();
}

inline void encode_PVFS_BMI_addr_t(char **pptr, const PVFS_BMI_addr_t *x)
{
    const char *addr_str;

    addr_str = BMI_addr_rev_lookup(*x);
    encode_string(pptr, &addr_str);
}

/* determines how much protocol space a BMI_addr_t encoding will consume */
inline int encode_PVFS_BMI_addr_t_size_check(const PVFS_BMI_addr_t *x)
{
    const char *addr_str;
    addr_str = BMI_addr_rev_lookup(*x);
    return(encode_string_size_check(&addr_str));
}

inline void decode_PVFS_BMI_addr_t(char **pptr, PVFS_BMI_addr_t *x)
{
    char *addr_string;
    decode_string(pptr, &addr_string);
    BMI_addr_lookup(x, addr_string);
}

inline void encode_PVFS_sys_layout(char **pptr, const struct PVFS_sys_layout_s *x)
{
    int tmp_size;
    int i;

    /* figure out how big this encoding will be first */

    tmp_size = 16; /* enumeration and list count */
    for(i=0 ; i<x->server_list.count; i++)
    {
        /* room for each server encoding */
        tmp_size += encode_PVFS_BMI_addr_t_size_check(&(x)->server_list.servers[i]);
    }

    if(tmp_size > PVFS_REQ_LIMIT_LAYOUT)
    {
        /* don't try to encode everything.  Just set pptr too high so that
         * we hit error condition in encode function
         */
        gossip_err("Error: layout too large to encode in request protocol.\n");
        *(pptr) += extra_size_PVFS_servreq_create + 1;
        return;
    }

    /* otherwise we are in business */
    encode_enum(pptr, &x->algorithm);
    encode_skip4(pptr, NULL);
    encode_int32_t(pptr, &x->server_list.count);
    encode_skip4(pptr, NULL);
    for(i=0 ; i<x->server_list.count; i++)
    {
        encode_PVFS_BMI_addr_t(pptr, &(x)->server_list.servers[i]);
    }
}

inline void decode_PVFS_sys_layout(char **pptr, struct PVFS_sys_layout_s *x)
{
    int i;

    decode_enum(pptr, &x->algorithm);
    decode_skip4(pptr, NULL);
    decode_int32_t(pptr, &x->server_list.count);
    decode_skip4(pptr, NULL);
    if(x->server_list.count)
    {
        x->server_list.servers = malloc(x->server_list.count*sizeof(*(x->server_list.servers)));
        assert(x->server_list.servers);
    }
    for(i=0 ; i<x->server_list.count; i++)
    {
        decode_PVFS_BMI_addr_t(pptr, &(x)->server_list.servers[i]);
    }
}

char *PINT_util_guess_alias(void)
{
    char tmp_alias[1024];
    char *tmpstr;
    char *alias;
    int ret;

    /* hmm...failed to find alias as part of the server config filename,
     * use the hostname to guess
     */
    ret = gethostname(tmp_alias, 1024);
    if(ret != 0)
    {
        gossip_err("Failed to get hostname while attempting to guess "
                   "alias.  Use -a to specify the alias for this server "
                   "process directly\n");
        return NULL;
    }
    alias = tmp_alias;

    tmpstr = strstr(tmp_alias, ".");
    if(tmpstr)
    {
        *tmpstr = 0;
    }
    return strdup(tmp_alias);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
