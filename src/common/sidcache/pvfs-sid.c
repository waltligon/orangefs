
/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  Functions for accessing SIDcache
 */



#include "gossip.h"
#include "pvfs2-debug.h"
#include "pvfs3-handle.h"
#include "pvfs2-types.h"
#include "quicklist.h"
#include "sidcache.h"
#include "policyeval.h"
#include "bmi.h"

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
    SID_cacheval_t *temp_cacheval;

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
        ret = BMI_addr_lookup(&temp_cacheval->bmi_addr, temp_cacheval->url);
        if (ret != 0)
        {
            return ret;
        }
        /* NULL enables overwrite of the record just looked up */
        ret = SID_cache_put(SID_db, sid, temp_cacheval, NULL);
        if (ret != 0)
        {
            return ret;
        }
    }
    *bmi_addr = temp_cacheval->bmi_addr;
    free(temp_cacheval);
    return ret;
}

/* These functions count servers of a given type
 */
static int PVFS_SID_count_server(int *count, struct SID_type_s stype)
{
    int ret = 0;
    DBT key, val;
    db_recno_t dbcountp = 0;
    int get_flags;

    if (!count)
    {
        return -PVFS_EINVAL;
    }

    SID_zero_dbt(&key, &val, NULL);

    gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                 "Counting servers of type %o\n", stype.server_type);

    if (stype.server_type == SID_SERVER_ALL)
    {
        key.data = &stype;
        key.size = 0;
        get_flags = DB_FIRST;
    }
    else 
    {
#     if 0
         if (stype.fsid)
         {
#     endif
        key.data = &stype;
        key.size = sizeof(stype);
        get_flags = DB_SET_RANGE;
#     if 0
        }
        else
        {
           key.data = &(stype.server_type);
           key.size = sizeof(stype.server_type);
           get_flags = DB_SET_RANGE;
        }
#     endif
    }
    key.ulen = sizeof(struct SID_type_s);
    key.flags = DB_DBT_USERMEM;
    *count = 0;

    /* val is left empty for DB to fill in */
   
    ret = SID_type_cursor->get(SID_type_cursor, /* type index */
                               &key,            /* key of secondary db  */
                               &val,            /* no val expected */
                               get_flags);      /* get flags */
    if(ret)
    {
        if (ret != DB_NOTFOUND)
        {
            gossip_err(/* GOSSIP_SIDCACHE_DEBUG, */
                       "Error getting type from type cache while counting: "
                       "%s\n", db_strerror(ret));
        }
        return ret;
    }
    ret = SID_type_cursor->count(SID_type_cursor, &dbcountp, 0);
    if(ret)
    {
        gossip_err(/* GOSSIP_SIDCACHE_DEBUG, */
                   "Error counting type from type cache while counting: "
                   "%s\n", db_strerror(ret));
        return ret;
    }
    *count = dbcountp;
    return 0;
}

int PVFS_SID_count_type(PVFS_fs_id fs_id, int type, int *count)
{
    struct SID_type_s stype = {type, fs_id};
    return PVFS_SID_count_server(count, stype);
}

/* This defines a bunch of easy to use counting functions */
#define DEFUN_COUNT( __NAME__ , __TYPE__ )           \
int __NAME__ (PVFS_fs_id fs_id, int *count)          \
{                                                    \
    int ret = 0;                                     \
    int mycount = 0;                                 \
    struct SID_type_s stype = {.fsid = fs_id, .server_type = __TYPE__ }; \
    ret = PVFS_SID_count_server(&mycount, stype);    \
    if (ret && ret != DB_NOTFOUND)                   \
    {                                                \
        return ret;                                  \
    }                                                \
    *count = mycount;                                \
    stype.fsid = 0;                                  \
    ret = PVFS_SID_count_server(&mycount, stype);    \
    if (ret && ret != DB_NOTFOUND)                   \
    {                                                \
        return ret;                                  \
    }                                                \
    *count += mycount;                               \
    return ret;                                      \
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
                               uint32_t flag)
{
    int ret = 0;
    DBT key, val;
    PVFS_SID sidval;
    SID_cacheval_t *temp_cacheval;

    SID_zero_dbt(&key, &val, NULL);

    gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                 "Searching for servers of type %o\n", stype.server_type);

/* SID_SERVER_ALL code is not right - figure out later */
#if 0
    if (stype.server_type == SID_SERVER_ALL)
    {
        key.data = &stype;
        key.size = 0;
        flag = DB_FIRST; /* override flag parameter */
    }
#endif
#if 0
    if (stype.fsid)
    {
#endif
        key.data = &stype;
        key.size = sizeof(stype);
#if 0
    }
    else
    {
        key.data = &(stype.server_type);
        key.size = sizeof(stype.server_type);
    }
#endif
    key.ulen = sizeof(struct SID_type_s);
    key.flags = DB_DBT_USERMEM;

    val.data = &sidval;
    val.size = sizeof(PVFS_SID);
    val.ulen = sizeof(PVFS_SID);
    val.flags = DB_DBT_USERMEM;

    /* val is left empty for DB to fill in */
   
    ret = SID_type_cursor->get(SID_type_cursor, /* type index */
                               &key,            /* key of secondary db  */
                               &val,            /* no val expected */
                               flag);           /* get flags */
    if(ret)
    {
        if (ret != DB_NOTFOUND)
        {
            gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                         "Error getting type from type cache while searching:"
                         "%s\n", db_strerror(ret));
        }
        return(ret);
    }

    /* This allocates memory for temp_cacheval */
    ret = SID_cache_get(SID_db, &sidval, &temp_cacheval);
    if(ret)
    {
        return(ret);
    }

    if (bmi_addr)
    {
        *bmi_addr = temp_cacheval->bmi_addr;
    }
    if (sid)
    {
        *sid = sidval;
    }
    free(temp_cacheval);
    return(ret);
}

int PVFS_SID_get_server_first(PVFS_BMI_addr_t *bmi_addr,
                              PVFS_SID *sid,
                              struct SID_type_s stype)
{
    return PVFS_SID_get_server(bmi_addr, sid, stype, DB_SET_RANGE);
}

int PVFS_SID_get_server_next(PVFS_BMI_addr_t *bmi_addr,
                             PVFS_SID *sid,
                             struct SID_type_s stype)
{
    return PVFS_SID_get_server(bmi_addr, sid, stype, DB_NEXT);
}

/* reads up to *n bmi addresses of type stype and sets *n to the number
 * actually read
 */
static int PVFS_SID_get_server_n(PVFS_BMI_addr_t *bmi_addr,
                                 PVFS_SID *sid,
                                 int *n,  /* inout */
                                 struct SID_type_s stype,
                                 int flag)
{
    int ret = 0;
    int i = 0;
    int32_t fs_id = stype.fsid; /* hold original fsid */
    PVFS_BMI_addr_t *badr = NULL;
    PVFS_SID *sa = NULL;
    unsigned int orig_type = 0;
    unsigned int tmask = 0;

    if (*n <= 0)
    {
        *n = 0;
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
                ret = PVFS_SID_get_server(badr, sa, stype, flag);
                if (ret && ret != DB_NOTFOUND)
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
                        ret = PVFS_SID_get_server(badr, sa, stype, DB_NEXT);
                        if (ret)
                        {
                            if (ret != DB_NOTFOUND)
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
    /* we don't return DB errors - an error means not found */
    return 0;
}

int PVFS_SID_get_server_first_n(PVFS_BMI_addr_t *bmi_addr,
                                PVFS_SID *sid,
                                int *n,
                                struct SID_type_s stype)
{
    return PVFS_SID_get_server_n(bmi_addr, sid, n, stype, DB_SET_RANGE);
}

int PVFS_SID_get_server_next_n(PVFS_BMI_addr_t *bmi_addr,
                               PVFS_SID *sid,
                               int *n,
                               struct SID_type_s stype)
{
    return PVFS_SID_get_server_n(bmi_addr, sid, n, stype, DB_NEXT);
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
                      uint32_t sid_count,
                      PVFS_SID **sid_array,
                      uint32_t datafile_count,
                      PVFS_handle **datafile_handles,
                      uint32_t datafile_sid_count,
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
                     uint32_t sid_count,
                     PVFS_SID **sid_array,
                     uint32_t dirdata_count,
                     PVFS_handle **dirdata_handles,
                     uint32_t dirdata_sid_count,
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

