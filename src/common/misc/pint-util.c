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

#ifdef WIN32
#include <io.h>
#include "wincommon.h"
#else
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#include "pvfs2-internal.h"
#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "gen-locks.h"
#include "pint-util.h"
#include "bmi.h"
#include "gossip.h"
#include "security-util.h"
#include <src/common/misc/pvfs2-internal.h>  /* lld(), llu() */
#include "pvfs2-req-proto.h"
#include "dist-dir-utils.h"

#include "pvfs2-debug.h"
#include "bmi-byteswap.h"

void PINT_time_mark(PINT_time_marker *out_marker)
{
#ifdef WIN32
    FILETIME creation, exit, system, user;
    ULARGE_INTEGER li_system, li_user;
#else
    struct rusage usage;
#endif

    gettimeofday(&out_marker->wtime, NULL);
#ifdef WIN32
    GetProcessTimes(GetCurrentProcess(), &creation, &exit, &system, &user);
    li_system.LowPart = system.dwLowDateTime;
    li_system.HighPart = system.dwHighDateTime;
    li_user.LowPart = user.dwLowDateTime;
    li_user.HighPart = user.dwHighDateTime;

    /* FILETIME is in 100-nanosecond increments */
    out_marker->stime.tv_sec = li_system.QuadPart / 10000000;
    out_marker->stime.tv_usec = (li_system.QuadPart % 10000000) / 10;
    out_marker->utime.tv_sec = li_system.QuadPart / 10000000;
    out_marker->utime.tv_usec = (li_system.QuadPart % 10000000) / 10;
#else
    getrusage(RUSAGE_SELF, &usage);
    out_marker->utime = usage.ru_utime;
    out_marker->stime = usage.ru_stime;
#endif
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

        if (src->mask & PVFS_ATTR_DISTDIR_ATTR)
        {
            PVFS_size dist_dir_bitmap_size, dirent_handles_array_size;

            PINT_dist_dir_attr_copyto(dest->dist_dir_attr, src->dist_dir_attr);

            dist_dir_bitmap_size = src->dist_dir_attr.bitmap_size *
                sizeof(PVFS_dist_dir_bitmap_basetype);
            if (dist_dir_bitmap_size)
            {
                if ((dest->mask & PVFS_ATTR_DISTDIR_ATTR) &&
                    dest->dist_dir_attr.num_servers > 0)
                {
                    if (dest->dist_dir_bitmap)
                    {
                        free(dest->dist_dir_bitmap);
                        dest->dist_dir_bitmap = NULL;
                    }
                }
                dest->dist_dir_bitmap = malloc(dist_dir_bitmap_size);
                if (!dest->dist_dir_bitmap)
                {
                    return ret;
                }
                memcpy(dest->dist_dir_bitmap,
                       src->dist_dir_bitmap, dist_dir_bitmap_size);
            }
            else
            {
                dest->dist_dir_bitmap = NULL;
            }

            dirent_handles_array_size = src->dist_dir_attr.num_servers *
                sizeof(PVFS_handle);

            if (dirent_handles_array_size)
            {
                if ((dest->mask & PVFS_ATTR_DISTDIR_ATTR) &&
                    dest->dist_dir_attr.num_servers > 0)
                {
                    if (dest->dirdata_handles)
                    {
                        free(dest->dirdata_handles);
                        dest->dirdata_handles = NULL;
                    }
                }
                dest->dirdata_handles = malloc(dirent_handles_array_size);
                if (!dest->dirdata_handles)
                {
                    return ret;
                }
                memcpy(dest->dirdata_handles,
                       src->dirdata_handles, dirent_handles_array_size);
            }
            else
            {
                dest->dirdata_handles = NULL;
            }
            dest->dist_dir_attr.num_servers = src->dist_dir_attr.num_servers;
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
                dest->u.dir.hint.dist_params 
                        = strdup(src->u.dir.hint.dist_params);
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

          if(src->mask & PVFS_ATTR_META_MIRROR_DFILES)
            {
                PVFS_size df_array_size = src->u.meta.dfile_count         *
                                          src->u.meta.mirror_copies_count *
                                          sizeof(PVFS_handle);

                if (df_array_size)
                {
                    if (   (dest->mask & PVFS_ATTR_META_MIRROR_DFILES) 
                        && (dest->u.meta.dfile_count > 0)
                        && (dest->u.meta.mirror_copies_count > 0) )
                    {
                        if (dest->u.meta.mirror_dfile_array)
                        {
                            free(dest->u.meta.mirror_dfile_array);
                            dest->u.meta.mirror_dfile_array = NULL;
                        }
                    }
                    dest->u.meta.mirror_dfile_array = malloc(df_array_size);
                    if (!dest->u.meta.mirror_dfile_array)
                    {
                        return ret;
                    }
                    memcpy(dest->u.meta.mirror_dfile_array,
                           src->u.meta.mirror_dfile_array, df_array_size);
                }
                else
                {
                    dest->u.meta.mirror_dfile_array = NULL;
                }
                dest->u.meta.mirror_copies_count 
                   = src->u.meta.mirror_copies_count;
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

        if (src->mask & PVFS_ATTR_CAPABILITY)
        {
            ret = PINT_copy_capability(&src->capability, &dest->capability);
            if (ret < 0)
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
        if (attr->mask & PVFS_ATTR_CAPABILITY)
        {
            if (attr->capability.signature)
            {
                free(attr->capability.signature);
            }            
            if (attr->capability.handle_array)
            {
                free(attr->capability.handle_array);
            }            
            if (attr->capability.issuer)
            {
                free(attr->capability.issuer);
            }
            memset(&attr->capability, 0, sizeof(PVFS_capability));
        }
        if (attr->mask & PVFS_ATTR_META_DFILES)
        {
            if (attr->u.meta.dfile_array)
            {
                free(attr->u.meta.dfile_array);
                attr->u.meta.dfile_array = NULL;
            }
        }
        if (attr->mask & PVFS_ATTR_META_MIRROR_DFILES)
        {
            if (attr->u.meta.mirror_dfile_array)
            {
                free(attr->u.meta.mirror_dfile_array);
                attr->u.meta.mirror_dfile_array = NULL;
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
        if (attr->mask & PVFS_ATTR_SYMLNK_TARGET)
        {
            if ((attr->u.sym.target_path_len > 0) &&
                attr->u.sym.target_path)
            {
                free(attr->u.sym.target_path);
                attr->u.sym.target_path = NULL;
            }
        }
        if ((attr->mask & PVFS_ATTR_DIR_HINT) || 
            (attr->mask & PVFS_ATTR_DIR_DIRENT_COUNT))
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
        if (attr->mask & PVFS_ATTR_DISTDIR_ATTR)
        {   
            if (attr->dist_dir_bitmap)
            {   
                free(attr->dist_dir_bitmap);
                attr->dist_dir_bitmap = NULL;
            }
            if (attr->dirdata_handles)
            {
                free(attr->dirdata_handles);
                attr->dirdata_handles = NULL;
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

/*
 * this is just a wrapper for gettimeofday
 */
void PINT_util_get_current_timeval(struct timeval *tv)
{
    gettimeofday(tv, NULL);
}

int PINT_util_get_timeval_diff(struct timeval *tv_start, struct timeval *tv_end)
{
    return (tv_end->tv_sec * 1e6 + tv_end->tv_usec) -
        (tv_start->tv_sec * 1e6 + tv_start->tv_usec);
}

/*
 * this returns time in seconds
 */
PVFS_time PINT_util_get_current_time(void)
{
    struct timeval t = {0,0};
    PVFS_time current_time = 0;

    gettimeofday(&t, NULL);
    current_time = (PVFS_time)t.tv_sec;
    return current_time;
}

/*
 * this gets time in ms - warning, can roll over
 */
PVFS_time PINT_util_get_time_ms(void)
{
    struct timeval t = {0,0};
    PVFS_time current_time = 0;

    gettimeofday(&t, NULL);
    current_time = ((PVFS_time)t.tv_sec) * 1000 + t.tv_usec / 1000;
    return current_time;
}

/*
 * this gets time in us - warning, can roll over
 */
PVFS_time PINT_util_get_time_us(void)
{
    struct timeval t = {0,0};
    PVFS_time current_time = 0;

    gettimeofday(&t, NULL);
    current_time = ((PVFS_time)t.tv_sec) * 1000000 + t.tv_usec;
    return current_time;
}

/* parses a struct timeval into a readable timestamp string*/
/* assumes sufficient memory has been allocated for str, no checking */
/* to be safe, make str a 64 character string atleast */
void PINT_util_parse_timeval(struct timeval tv, char *str)
{
    time_t now;
    struct tm *currentTime;

    now = tv.tv_sec;
    currentTime = localtime(&now);
    strftime(str, 64, "%m/%d/%Y %H:%M:%S", currentTime);

    return;
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
#ifdef WIN32
    result.tv_sec = add.tv_sec + now.tv_sec;
    result.tv_usec = add.tv_usec + now.tv_usec;
    if (result.tv_usec >= 1000000)
    {
        result.tv_usec -= 1000000;
        result.tv_sec++;
    }
#else
    timeradd(&now, &add, &result);
#endif
    tv.tv_sec = result.tv_sec;
    tv.tv_nsec = result.tv_usec * 1e3;
    return tv;
}

PVFS_uid PINT_util_getuid(void)
{
#ifdef WIN32
    /* TODO! */
    return (PVFS_uid) 999;
#else
    return (PVFS_uid) getuid();
#endif
}

PVFS_gid PINT_util_getgid(void)
{
#ifdef WIN32
    /* TODO! */
    return (PVFS_gid) 999;
#else
    return (PVFS_gid) getgid();
#endif
}

/*                                                              
 * Output hex representation of arbitrary data.
 * The output buffer must have size for count * 2 bytes + 1 (zero-byte).
 */
char *PINT_util_bytes2str(unsigned char *bytes, char *output, size_t count)
{
    unsigned char *in_p;
    char *out_p;
    size_t i, num;

    if (!bytes || !output || !count)
    {
        return NULL;
    }

    for (in_p = bytes, out_p = output, i = 0;
         i < count;
         in_p++, out_p += num, i++)
    {
        num = sprintf(out_p, "%02x", *in_p);
    }

    return output;    

}

#ifndef WIN32
inline
#endif
void encode_PVFS_BMI_addr_t(char **pptr, const PVFS_BMI_addr_t *x)
{
    const char *addr_str;

    addr_str = BMI_addr_rev_lookup(*x);
    encode_string(pptr, &addr_str);
}

/* determines how much protocol space a BMI_addr_t encoding will consume */
#ifndef WIN32
inline
#endif
int encode_PVFS_BMI_addr_t_size_check(const PVFS_BMI_addr_t *x)
{
    const char *addr_str;
    addr_str = BMI_addr_rev_lookup(*x);
    return(encode_string_size_check(&addr_str));
}
#ifndef WIN32
inline
#endif
void decode_PVFS_BMI_addr_t(char **pptr, PVFS_BMI_addr_t *x)
{
    char *addr_string;
    decode_string(pptr, &addr_string);
    BMI_addr_lookup(x, addr_string);
}

#ifndef WIN32
inline
#endif
void encode_PVFS_sys_layout(char **pptr, const struct PVFS_sys_layout_s *x)
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

#ifndef WIN32
inline
#endif
void decode_PVFS_sys_layout(char **pptr, struct PVFS_sys_layout_s *x)
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

    tmpstr = strstr(tmp_alias, ".");
    if(tmpstr)
    {
        *tmpstr = 0;
    }
    return strdup(tmp_alias);
}

/* TODO: orange security 
   These functions aren't used with the new security code. 
   However they may be repurposed later. */
#if 0

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
#ifdef HAVE_GETPWUID
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
        check_group_pw_buffer = (char*)malloc(pw_buf_size);
        check_group_gr_buffer = (char*)malloc(gr_buf_size);
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

    /* get the members of the group */
    ret = getgrgid_r(gid, &grp, check_group_gr_buffer, gr_buf_size, &grp_p);
    if(ret != 0 || grp_p == NULL)
    {
      gen_mutex_unlock(&check_group_mutex);
      gossip_err("Get members for group (gid=%d) failed."
                 "errno [%d] error_msg [%s]\n",
                 gid, ret, strerror(ret));
      return(-PVFS_EINVAL);
    }

    for(i = 0; grp.gr_mem[i] != NULL; i++)
    {
        if(0 == strcmp(pwd.pw_name, grp.gr_mem[i]))
        {
            gen_mutex_unlock(&check_group_mutex);
            return 0;
        } 
    }

    gen_mutex_unlock(&check_group_mutex);
    return(-PVFS_ENOENT);
#else
    return 0;
#endif
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

/* WARNING!  THIS CODE SUCKS!  REWRITE WITHOUT GOTOS PLEASE! */
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
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG,
                    "no acl's present.. denying access\n");
        return -PVFS_EACCES;
    }

    /* keyval for ACLs includes a \0. so subtract the thingie */
#ifdef PVFS_USE_OLD_ACL_FORMAT
    acl_size--;
#else
    acl_size -= sizeof(pvfs2_acl_header);
#endif
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG,
                "PINT_check_acls: read keyval size "
                " %d (%d acl entries)\n",
                (int) acl_size, 
                (int) (acl_size / sizeof(pvfs2_acl_entry)));
    gossip_debug(GOSSIP_PERMISSIONS_DEBUG,
                "uid = %d, gid = %d, want = %d\n",
                uid,
                gid,
                want);

    assert(acl_buf);
    /* if the acl format doesn't look valid,
     * then return an error rather than
     * asserting; we don't want the server
     * to crash due to an invalid keyval
     */
    if((acl_size % sizeof(pvfs2_acl_entry)) != 0)
    {
        gossip_debug(GOSSIP_PERMISSIONS_DEBUG, "invalid acls on object\n");
        return(-PVFS_EACCES);
    }
    count = acl_size / sizeof(pvfs2_acl_entry);

    for (i = 0; i < count; i++)
    {
#ifdef PVFS_USE_OLD_ACL_FORMAT
        pa = (pvfs2_acl_entry *) acl_buf + i;
#else
        pa = &(((pvfs2_acl_header *)acl_buf)->p_entries[i]);
#endif
        /* 
           NOTE: Remember that keyval is encoded as lebf,
           so convert it to host representation 
        */
#ifdef PVFS_USE_OLD_ACL_FORMT
        pe.p_tag  = bmitoh32(pa->p_tag);
        pe.p_perm = bmitoh32(pa->p_perm);
        pe.p_id   = bmitoh32(pa->p_id);
#else
        pe.p_tag  = bmitoh16(pa->p_tag);
        pe.p_perm = bmitoh16(pa->p_perm);
        pe.p_id   = bmitoh32(pa->p_id);
#endif
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
        pvfs2_acl_entry me;
#ifdef PVFS_USE_OLD_ACL_FORMAT
        pvfs2_acl_entry *mask_obj = (pvfs2_acl_entry *) acl_buf + i;
#else
        pvfs2_acl_entry *mask_obj =
                &(((pvfs2_acl_header *)acl_buf)->p_entries[i]);
#endif
        
        /* 
          NOTE: Again, since pvfs2_acl_entry is in lebf, we need to
          convert it to host endian format
         */
#ifdef PVFS_USE_OLD_ACL_FORMAT
        me.p_tag  = bmitoh32(mask_obj->p_tag);
        me.p_perm = bmitoh32(mask_obj->p_perm);
        me.p_id   = bmitoh32(mask_obj->p_id);
#else
        me.p_tag  = bmitoh16(mask_obj->p_tag);
        me.p_perm = bmitoh16(mask_obj->p_perm);
        me.p_id   = bmitoh32(mask_obj->p_id);
#endif
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

#endif /* #if 0 */

#ifdef WIN32
int PINT_statfs_lookup(const char *path, struct statfs *buf)
{
    char *abs_path, *root_path; 
    int rc, start, index, slash_max, slash_count;
    DWORD sect_per_cluster, bytes_per_sect, free_clusters, total_clusters;

    if (path == NULL || buf == NULL) 
    {
        errno = EFAULT;
        return -1;
    }
    
    /* allocate a buffer to get an absolute path */
    abs_path = (char *) malloc(MAX_PATH + 1);
    if (_fullpath(abs_path, path, MAX_PATH) == NULL)
    {
        free(abs_path);
        errno = ENOENT;
        return -1;
    }

    /* allocate buffer for root path */
    root_path = (char *) malloc(strlen(abs_path) + 1);

    /* parse out the root directory--it will be in
       \\MyServer\MyFolder\ form or C:\ form */
    if (abs_path[0] == '\\' && abs_path[1] == '\\')
    {
        start = 2;
        slash_max = 2;
    }
    else 
    {
        start = 0;
        slash_max = 1;
    }

    slash_count = 0;
    index = start;

    while (abs_path[index] && slash_count < slash_max)
    {
        if (abs_path[index++] == '\\')
            slash_count++;
    }

    /* copy root path */
    strncpy_s(root_path, strlen(abs_path)+1, abs_path, index);

    rc = 0;
    if (GetDiskFreeSpace(root_path, &sect_per_cluster, &bytes_per_sect,
                          &free_clusters, &total_clusters))
    {
        buf->f_type = 0;  /* not used by PVFS */
        buf->f_bsize = (uint64_t) sect_per_cluster * bytes_per_sect;
        buf->f_bavail = buf->f_bfree = (uint64_t) free_clusters;
        buf->f_blocks = (uint64_t) total_clusters;
        buf->f_fsid = 0;  /* no meaningful definition on Windows */
    }
    else
    {
        errno = GetLastError();
        rc = -1;
    }

    free(root_path);
    free(abs_path);

    return rc;

}

int PINT_statfs_fd_lookup(int fd, struct statfs *buf)
{
    HANDLE handle;
    char *path;
    int rc;

    /* get handle from fd */
    handle = (HANDLE) _get_osfhandle(fd);

    /* get file path from handle */
    path = (char *) malloc(MAX_PATH + 1);
    /* Note: only available on Vista/WS2008 and later */
    if (GetFinalPathNameByHandle(handle, path, MAX_PATH, 0) != 0)
    {
        free(path);
        errno = GetLastError();
        return -1;
    }

    rc = PINT_statfs_lookup(path, buf);

    free(path);

    return rc;

}

#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
