
/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  Functions for accessing SIDcache
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "gossip.h"
#include "pvfs2-debug.h"
#include "pvfs3-handle.h"
#include "pvfs2-types.h"
#include "quicklist.h"
#include "sidcache.h"
#include "policyeval.h"
#include "bmi.h"
#include "pint-malloc.h"

/* V3 I think this is obsolete - get rid of it */
#if 0
enum {
    PVFS_OBJ_META,
    PVFS_OBJ_DATA,
    PVFS_OBJ_FILE
};
#endif

static int PVFS_OBJ_gen(PVFS_object_ref *obj,
                        int obj_count,
                        PVFS_fs_id fs_id,
                        int type);

/**
 * This routine runs a policy query against the sid cache to select
 * a sid for one or more new metadata objects (inode, dir, symlink, etc.)
 */
int PVFS_OBJ_gen_meta(PVFS_object_ref *obj,
                      int obj_count,
                      PVFS_fs_id fs_id)
{
    int ret = 0;
    ret = PVFS_OBJ_gen(obj, obj_count, fs_id, SID_SERVER_META);
    return ret;
}

/**
 * This routine runs a policy query against the sid cache to select
 * a sid for one or more new datafile objects
 */
int PVFS_OBJ_gen_data(PVFS_object_ref *obj,
                      int obj_count,
                      PVFS_fs_id fs_id)
{
    int ret = 0;
    ret = PVFS_OBJ_gen(obj, obj_count, fs_id, SID_SERVER_DATA);
    return ret;
}

/**
 * The work of getting an OID and SIDS is done here.  Among other
 * things we have to select the right policy, which indicates how many
 * replicants we want, run the query and set up all of the fields in the
 * object_ref struct.  We might need to pass in some info if it isn't
 * all in the policy/sidcache
 */
static int PVFS_OBJ_gen(PVFS_object_ref *obj_array,
                        int obj_count,
                        PVFS_fs_id fs_id,
                        int type)
{
    int ret = 0;
    int i, s;
    int num_copies;
    SID_server_list_t svr, *svr_p;

    if (obj_array == NULL || fs_id == PVFS_FS_ID_NULL || obj_count < 1 ||
        type < 0 || type > PVFS_POLICY_MAX)
    {
        errno = EINVAL;
        return -1;
    }
    INIT_QLIST_HEAD(&svr.link);
    /* run query for SIDs here */
    ret = SID_select_servers(&SID_policies[type],
                             obj_count,
                             &num_copies,
                             &svr);
    if (ret < 0)
    {
        return ret;
    }
    /* clear the output array */
    memset(obj_array, 0, obj_count * sizeof(PVFS_object_ref));
    svr_p = &svr;
    /* for each object */
    for (i = 0; i < obj_count; i++)
    {
        /* set up the main object */
        obj_array[i].fs_id = fs_id;
        PVFS_OID_gen(&(obj_array[i].handle));
        /* set up SIDs */
        obj_array[i].sid_count = num_copies;
        obj_array[i].sid_array = (PVFS_SID *)malloc(sizeof(PVFS_SID) *
                                                    num_copies);
        if (obj_array[i].sid_array == NULL)
        {
            ret = -1;
            goto errorout;
        }
        /* clear SID array */
        ZEROMEM(obj_array[i].sid_array, sizeof(PVFS_SID) * num_copies);
        /* loop through servers assigning one to each SID */
        for (s = 0; s < num_copies; s++)
        {
            obj_array[i].sid_array[s] = svr_p->server_sid;
            svr_p = qlist_entry(svr_p->link.next, SID_server_list_t, link);
        }
    }
    return ret;
errorout:
    for (i = 0; i < obj_count; i++)
    {
        if (obj_array[i].sid_array != NULL)
        {
            free(obj_array[i].sid_array);
            obj_array[i].sid_array = NULL;
        }
    }
    return ret;
}

/**
 * Look up the SID provided and return the matching BMI address
 */
int PVFS_SID_get_addr(PVFS_BMI_addr_t *bmi_addr, const PVFS_SID *sid)
{
    int ret;
    SID_cacheval_t *temp_cacheval = NULL;

    if (!bmi_addr || !sid)
    {
        return -PVFS_EINVAL;
    }
    /* with SID we can look up BMI_addr if it is there */
    /* and the id_string URI if not - then lookup with BMI */
    ret = SID_cache_get(SID_db, sid, &temp_cacheval);
    if (ret != 0)
    {
        return ret;
    }
    if (temp_cacheval->bmi_addr == 0)
    {
        /* enter url into BMI to get BMI addr */
        ret = BMI_addr_lookup(&(temp_cacheval->bmi_addr),
                              temp_cacheval->url,
                              NULL);
        if (ret == 0)
        {
            /* write back the bmi_addr we just looked up to sidcache */
            /* NULL enables overwrite of the record just looked up */
            ret = SID_cache_put(SID_db, sid, temp_cacheval, NULL);
        }
    }
    if (ret == 0)
    {
        *bmi_addr = temp_cacheval->bmi_addr;
    }
    SID_cacheval_free(&temp_cacheval);
    return ret;
}

/* Position of the last type-database read. A cursor cannot stay open
 * between calls, so the next read resumes from this key and SID.
 */
static int scan_valid = 0;
static struct SID_type_s scan_key;
static PVFS_SID scan_sid;

static int type_equal(const struct SID_type_s *a, const struct SID_type_s *b)
{
    return a->fsid == b->fsid && a->server_type == b->server_type;
}

static int sid_seen(const PVFS_SID *sid, const PVFS_SID *list, int n)
{
    int i;

    for (i = 0; i < n; i++)
    {
        if (memcmp(&list[i], sid, sizeof(*sid)) == 0)
        {
            return 1;
        }
    }
    return 0;
}

/* A record with fsid 0 applies to every file system. Count each SID
 * once when it has both that record and one for the requested fsid.
 * SID_SERVER_ALL matches every server type. Replication decides how
 * many of these servers an object uses. It does not make a second SID
 * for the same server.
 */
static int remember_sid(PVFS_SID **seen, int *seen_n, const PVFS_SID *sid)
{
    PVFS_SID *grown;

    if (sid_seen(sid, *seen, *seen_n))
    {
        return 0;
    }
    grown = (PVFS_SID *)realloc(*seen,
                                (size_t)(*seen_n + 1) * sizeof(PVFS_SID));
    if (!grown)
    {
        return -PVFS_ENOMEM;
    }
    *seen = grown;
    memcpy(&(*seen)[*seen_n], sid, sizeof(PVFS_SID));
    (*seen_n)++;
    return 0;
}

static int collect_exact(sid_cursor *cursorp,
                         struct SID_type_s stype,
                         PVFS_SID **seen,
                         int *seen_n)
{
    struct sid_data key;
    struct sid_data val;
    int rc;
    int op;

    op = SID_DB_CURSOR_SET;
    while (1)
    {
        SID_zero_dbt(&key, &val, NULL);
        key.data = &stype;
        key.len = sizeof(stype);
        rc = sid_db_cursor_get(cursorp, &key, &val, op);
        if (rc == -PVFS_ENOENT)
        {
            return 0;
        }
        if (rc)
        {
            return rc;
        }
        if (val.len == sizeof(PVFS_SID) && val.data)
        {
            rc = remember_sid(seen, seen_n, (const PVFS_SID *)val.data);
        }
        else
        {
            rc = 0;
        }
        free(val.data);
        if (rc)
        {
            return rc;
        }
        op = SID_DB_CURSOR_NEXT_DUP;
    }
}

static int collect_fs(sid_cursor *cursorp,
                      PVFS_fs_id fs_id,
                      PVFS_SID **seen,
                      int *seen_n)
{
    struct sid_data key;
    struct sid_data val;
    int rc;
    int op;

    op = SID_DB_CURSOR_FIRST;
    while (1)
    {
        SID_zero_dbt(&key, &val, NULL);
        rc = sid_db_cursor_get(cursorp, &key, &val, op);
        if (rc == -PVFS_ENOENT)
        {
            return 0;
        }
        if (rc)
        {
            free(key.data);
            free(val.data);
            return rc;
        }
        rc = 0;
        if (key.len == sizeof(struct SID_type_s) &&
            val.len == sizeof(PVFS_SID) &&
            key.data && val.data)
        {
            struct SID_type_s type;

            memcpy(&type, key.data, sizeof(type));
            if (type.fsid == fs_id || (fs_id != 0 && type.fsid == 0))
            {
                rc = remember_sid(seen, seen_n, (const PVFS_SID *)val.data);
            }
        }
        free(key.data);
        free(val.data);
        if (rc)
        {
            return rc;
        }
        op = SID_DB_CURSOR_NEXT;
    }
}

/* These functions count servers of a given type
 */
static int PVFS_SID_count_server(int *count,
                                 PVFS_fs_id fs_id,
                                 uint32_t server_type)
{
    sid_cursor *cursorp = NULL;
    PVFS_SID *seen = NULL;
    int seen_n = 0;
    struct SID_type_s stype;
    int rc;

    if (!count)
    {
        return -PVFS_EINVAL;
    }
    *count = 0;
    gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                 "Counting servers of type %o\n", server_type);
    if (!SID_type_db)
    {
        return -PVFS_EINVAL;
    }
    rc = sid_db_cursor(SID_type_db, &cursorp, 1);
    if (rc)
    {
        return rc;
    }
    if (server_type == SID_SERVER_ALL)
    {
        rc = collect_fs(cursorp, fs_id, &seen, &seen_n);
    }
    else
    {
        stype.fsid = fs_id;
        stype.server_type = server_type;
        rc = collect_exact(cursorp, stype, &seen, &seen_n);
        if (!rc && fs_id != 0)
        {
            stype.fsid = 0;
            rc = collect_exact(cursorp, stype, &seen, &seen_n);
        }
    }
    sid_db_cursor_close(cursorp);
    free(seen);
    if (rc)
    {
        return rc;
    }
    *count = seen_n;
    return 0;
}

int PVFS_SID_count_type(PVFS_fs_id fs_id, int type, int *count)
{
    return PVFS_SID_count_server(count, fs_id, (uint32_t)type);
}

/* This defines a bunch of easy to use counting functions */
#define DEFUN_COUNT( __NAME__ , __TYPE__ )           \
int __NAME__ (PVFS_fs_id fs_id, int *count)          \
{                                                    \
    return PVFS_SID_count_server(count, fs_id, __TYPE__); \
}

DEFUN_COUNT(PVFS_SID_count_all, SID_SERVER_ALL)
DEFUN_COUNT(PVFS_SID_count_io, SID_SERVER_DATA)
DEFUN_COUNT(PVFS_SID_count_meta, SID_SERVER_META)
DEFUN_COUNT(PVFS_SID_count_dirm, SID_SERVER_DIRM)
DEFUN_COUNT(PVFS_SID_count_dird, SID_SERVER_DIRD)
DEFUN_COUNT(PVFS_SID_count_root, SID_SERVER_ROOT)
DEFUN_COUNT(PVFS_SID_count_prime, SID_SERVER_PRIME)
DEFUN_COUNT(PVFS_SID_count_config, SID_SERVER_CONFIG)

#undef DEFUN_COUNT

/* These functions find servers of a given type
 */
static int PVFS_SID_get_server(PVFS_BMI_addr_t *bmi_addr,
                               PVFS_SID *sid,
                               struct SID_type_s stype,
                               int first)
{
    sid_cursor *cursorp = NULL;
    struct sid_data key;
    struct sid_data val;
    int ret = 0;
    int copied = 0;
    PVFS_SID sidval;

    gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                 "Searching for servers of type %o\n", stype.server_type);
    if (!SID_type_db)
    {
        return -PVFS_EINVAL;
    }
    ret = sid_db_cursor(SID_type_db, &cursorp, 1);
    if (ret)
    {
        return ret;
    }
    SID_zero_dbt(&key, &val, NULL);
    key.data = &stype;
    key.len = sizeof(stype);
    if (first || !scan_valid || !type_equal(&scan_key, &stype))
    {
        ret = sid_db_cursor_get(cursorp, &key, &val, SID_DB_CURSOR_SET);
        copied = (ret == 0);
    }
    else
    {
        val.data = &scan_sid;
        val.len = sizeof(scan_sid);
        ret = sid_db_cursor_get(cursorp, &key, &val, SID_DB_CURSOR_GET_BOTH);
        if (ret == 0)
        {
            val.data = NULL;
            val.len = 0;
            ret = sid_db_cursor_get(cursorp, &key, &val,
                                    SID_DB_CURSOR_NEXT_DUP);
            copied = (ret == 0);
        }
    }
    sid_db_cursor_close(cursorp);
    if (ret || !copied || val.len != sizeof(PVFS_SID) || !val.data)
    {
        if (copied)
        {
            free(val.data);
        }
        if (ret != -PVFS_ENOENT)
        {
            gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                         "Error getting type from type cache while searching:"
                         " %d\n", ret);
        }
        return ret ? ret : -PVFS_ENOENT;
    }
    memcpy(&sidval, val.data, sizeof(sidval));
    free(val.data);
    scan_key = stype;
    scan_sid = sidval;
    scan_valid = 1;

    /* no point in the cacheval unless we are going for the bmi_addr
     * so only bother with this get if we are
     */
    if (bmi_addr)
    {
        ret = PVFS_SID_get_addr(bmi_addr, &sidval);
        if (ret)
        {
            return ret;
        }
    }
    if (sid)
    {
        *sid = sidval;
    }
    return 0;
}

/* These routines are used when finding servers of a given type
 * _first involves finding the first such server, as defined by
 * type DB, and next is for find the next as with a cursor.
 * A key change restarts the scan, so a next call for a different
 * type still returns the first server of that type.
 */
int PVFS_SID_get_server_first(PVFS_BMI_addr_t *bmi_addr,
                              PVFS_SID *sid,
                              struct SID_type_s stype)
{
    return PVFS_SID_get_server(bmi_addr, sid, stype, 1);
}

int PVFS_SID_get_server_next(PVFS_BMI_addr_t *bmi_addr,
                             PVFS_SID *sid,
                             struct SID_type_s stype)
{
    return PVFS_SID_get_server(bmi_addr, sid, stype, 0);
}

/* reads up to *n bmi addresses of type stype and sets *n to the number
 * actually read
 */
static int PVFS_SID_get_server_n(PVFS_BMI_addr_t *bmi_addr,
                                 PVFS_SID *sid,
                                 int *n,  /* inout */
                                 struct SID_type_s stype,
                                 int first)
{
    int ret = 0;
    int i = 0;
    int32_t fs_id = stype.fsid; /* hold original fsid */
    PVFS_BMI_addr_t *badr = NULL;
    PVFS_SID *sa = NULL;
    unsigned int orig_type = 0;
    unsigned int tmask = 0;

    if (!n || *n <= 0)
    {
        if (n)
        {
            *n = 0;
        }
        return -PVFS_EINVAL;
    }
    /* loop for each type included in stype */
    orig_type = stype.server_type;
    tmask = SID_SERVER_ME;
    /* *n is the number we want.  i is the number we have found */
    for (i = 0; orig_type && tmask && i < *n; tmask >>= 1)
    {
        if (orig_type & tmask)
        {
            int try;
            badr = bmi_addr ? &bmi_addr[i] : NULL;
            sa = sid ? &sid[i] : NULL;
            stype.server_type = tmask;
            for (try = 0; try < 2 && i < *n; try++)
            {
                ret = PVFS_SID_get_server(badr, sa, stype, first);
                if (ret && ret != -PVFS_ENOENT)
                {
                    gossip_err("Error looking for a server in sidcache\n");
                }
                else if (!ret)
                {
                    /* found item, no error */
                    for (i++; i < *n; i++)
                    {
                        badr = bmi_addr ? &bmi_addr[i] : NULL;
                        sa = sid ? &sid[i] : NULL;
                        ret = PVFS_SID_get_server(badr, sa, stype, 0);
                        if (ret)
                        {
                            if (ret != -PVFS_ENOENT)
                            {
                                gossip_err("Error looking for a server in sidcache\n");
                            }
                            /* not found or error */
                            break;
                        }
                    }
                }
                if (try == 0 && i < *n)
                {
                    stype.fsid = 0;
                }
                else
                {
                    stype.fsid = fs_id;
                }
            }
            /* clear bit from orig_type */
            orig_type &= ~tmask;
        }
    }
    /* reset n to the number actually found */
    *n = i;
    /* an error means not found; the count is in *n */
    return 0;
}

int PVFS_SID_get_server_first_n(PVFS_BMI_addr_t *bmi_addr,
                                PVFS_SID *sid,
                                int *n,
                                struct SID_type_s stype)
{
    return PVFS_SID_get_server_n(bmi_addr, sid, n, stype, 1);
}

int PVFS_SID_get_server_next_n(PVFS_BMI_addr_t *bmi_addr,
                               PVFS_SID *sid,
                               int *n,
                               struct SID_type_s stype)
{
    return PVFS_SID_get_server_n(bmi_addr, sid, n, stype, 0);
}

/******************************************
 * These are higher-level policy generators
 * ****************************************/

/**
 * These routine runs various policy quieries to allocated the OIDs and
 * SIDs needed for a file.  
 */

/**
 * Simple default policy just picks them in order found in the DB
 */
int PVFS_OBJ_gen_file(PVFS_fs_id fs_id,
                      PVFS_handle **handle,
                      int32_t sid_count,
                      PVFS_SID **sid_array,
                      uint32_t datafile_count,
                      PVFS_handle **datafile_handles,
                      int32_t datafile_sid_count,
                      PVFS_SID **datafile_sid_array)
{
    int ret = 0;
    int n;
    int i;
    struct SID_type_s meta_server = {.server_type = SID_SERVER_META, .fsid = 0};
    struct SID_type_s data_server = {.server_type = SID_SERVER_DATA, .fsid = 0};

    /* set acutal fs_id requested */
    meta_server.fsid = fs_id;
    data_server.fsid = fs_id;

    /* generate metadata handle */
    *handle = malloc(sizeof(PVFS_handle));
    PVFS_OID_gen(*handle);

    /* generate SIDs for metadata object */
    n = sid_count;
    *sid_array = (PVFS_SID *)malloc(n * sizeof(PVFS_SID));
    PVFS_SID_get_server_first_n(NULL, *sid_array, &n, meta_server);

    /* generate datafile handles */
    *datafile_handles = (PVFS_OID *)malloc(datafile_count * sizeof(PVFS_OID));
    for (i = 0; i < datafile_count; i++)
    {
        PVFS_OID_gen(&(*datafile_handles)[i]);
    }

    /* generate SIDs for datafile objects */
    n = datafile_sid_count * datafile_count;
    *datafile_sid_array = (PVFS_SID *)malloc(n * sizeof(PVFS_SID));
    PVFS_SID_get_server_next_n(NULL, *datafile_sid_array, &n, data_server);

    return ret;
}

/**
 * Simple default policy just picks them in order found in the DB
 */
int PVFS_OBJ_gen_dir(PVFS_fs_id fs_id,
                     PVFS_handle **handle,
                     int32_t sid_count,
                     PVFS_SID **sid_array,
                     uint32_t dirdata_count,
                     PVFS_handle **dirdata_handles,
                     int32_t dirdata_sid_count,
                     PVFS_SID **dirdata_sid_array)
{
    int ret = 0;
    int n;
    int i;
    struct SID_type_s dirm_server = {.server_type = SID_SERVER_META, .fsid = 0};
    struct SID_type_s dird_server = {.server_type = SID_SERVER_DIRD, .fsid = 0};

    /* set acutal fs_id requested */
    dirm_server.fsid = fs_id;
    dird_server.fsid = fs_id;

    /* generate metadata handle */
    *handle = malloc(sizeof(PVFS_handle));
    PVFS_OID_gen(*handle);

    /* generate SIDs for metadata object */
    n = sid_count;
    *sid_array = (PVFS_SID *)malloc(n * sizeof(PVFS_SID));
    PVFS_SID_get_server_first_n(NULL, *sid_array, &n, dirm_server);

    /* generate dirdata handles */
    *dirdata_handles = (PVFS_OID *)malloc(dirdata_count * sizeof(PVFS_OID));
    for (i = 0; i < dirdata_count; i++)
    {
        PVFS_OID_gen(*dirdata_handles);
    }

    /* generate SIDs for dirdata objects */
    n = dirdata_sid_count * dirdata_count;
    *dirdata_sid_array = (PVFS_SID *)malloc(n * sizeof(PVFS_SID));
    PVFS_SID_get_server_next_n(NULL, *dirdata_sid_array, &n, dird_server);
    return ret;
}
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
