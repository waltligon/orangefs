
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
    if (temp_cacheval->bmi_addr == 0);
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
static int PVFS_SID_count_server(int *count, uint32_t stype)
{
    int ret = 0;
    DBT key, val;
    db_recno_t dbcountp = 0;

    if (!count)
    {
        return -PVFS_EINVAL;
    }

    SID_zero_dbt(&key, &val, NULL);

    gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                 "Counting servers of type %o\n", stype);

    key.data = &stype;
    key.size = sizeof(uint32_t);
    key.ulen = sizeof(uint32_t);
    key.flags = DB_DBT_USERMEM;

    /* val is left empty for DB to fill in */
   
    ret = SID_type_cursor->get(SID_type_cursor, /* type index */
                               &key,            /* key of secondary db  */
                               &val,            /* no val expected */
                               DB_SET);         /* get flags */
    if(ret)
    {
        if (ret != DB_NOTFOUND)
        {
            gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                         "Error getting type from type cache while counting: "
                         "%s\n", db_strerror(ret));
        }
        return ret;
    }
    ret = SID_type_cursor->count(SID_type_cursor, &dbcountp, 0);
    if(ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "Error counting type from type cache while counting: "
                     "%s\n", db_strerror(ret));
        return ret;
    }
    *count = dbcountp;
    return 0;
}

int PVFS_SID_count_io(PVFS_fs_id fs_id, int *count)
{
    return PVFS_SID_count_server(count, SID_SERVER_DATA);
}

int PVFS_SID_count_meta(PVFS_fs_id fs_id, int *count)
{
    return PVFS_SID_count_server(count, SID_SERVER_META);
}

int PVFS_SID_count_root(PVFS_fs_id fs_id, int *count)
{
    return PVFS_SID_count_server(count, SID_SERVER_ROOT);
}

int PVFS_SID_count_prime(PVFS_fs_id fs_id, int *count)
{
    return PVFS_SID_count_server(count, SID_SERVER_PRIME);
}

int PVFS_SID_count_config(PVFS_fs_id fs_id, int *count)
{
    return PVFS_SID_count_server(count, SID_SERVER_CONFIG);
}

/* These functions find servers of a given type
 */
static int PVFS_SID_get_server(PVFS_BMI_addr_t *bmi_addr,
                               PVFS_SID *sid,
                               uint32_t stype,
                               uint32_t flag)
{
    int ret = 0;
    DBT key, val;
    PVFS_SID sidval;
    SID_cacheval_t *temp_cacheval;

    SID_zero_dbt(&key, &val, NULL);

    gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                 "Searching for servers of type %o\n", stype);

    key.data = &stype;
    key.size = sizeof(uint32_t);
    key.ulen = sizeof(uint32_t);
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
                              uint32_t stype)
{
    return PVFS_SID_get_server(bmi_addr, sid, stype, DB_SET);
}

int PVFS_SID_get_server_next(PVFS_BMI_addr_t *bmi_addr,
                             PVFS_SID *sid,
                             uint32_t stype)
{
    return PVFS_SID_get_server(bmi_addr, sid, stype, DB_NEXT);
}

/* reads up to *n bmi addresses of type stype and sets *n to the number
 * actually read
 */
static int PVFS_SID_get_server_n(PVFS_BMI_addr_t *bmi_addr,
                                 PVFS_SID *sid,
                                 int *n,  /* inout */
                                 uint32_t stype,
                                 int flag)
{
    int ret = 0;
    int i = 0;

    if (*n <= 0)
    {
        *n = 0;
        return -PVFS_EINVAL;
    }
    ret = PVFS_SID_get_server(bmi_addr, sid, stype, flag);
    if (!ret)
    {
        for (i = 1; i < *n; i++)
        {
            PVFS_BMI_addr_t *badr = bmi_addr ? &bmi_addr[i] : NULL;
            PVFS_SID *sa = sid ? &sid[i] : NULL;
            ret = PVFS_SID_get_server(badr, sa, stype, DB_NEXT);
            if (ret)
            {
                break;
            }
        }
    }
    *n = i;
    return ret;
}

int PVFS_SID_get_server_first_n(PVFS_BMI_addr_t *bmi_addr,
                                PVFS_SID *sid,
                                int *n,
                                uint32_t stype)
{
    return PVFS_SID_get_server_n(bmi_addr, sid, n, stype, DB_SET);
}

int PVFS_SID_get_server_next_n(PVFS_BMI_addr_t *bmi_addr,
                               PVFS_SID *sid,
                               int *n,
                               uint32_t stype)
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

    /* generate metadata handle */
    *handle = malloc(sizeof(PVFS_handle));
    PVFS_OID_gen(*handle);

    /* generate SIDs for metadata object */
    *sid_array = (PVFS_SID *)malloc(sid_count * sizeof(PVFS_SID));
    n = sid_count;
    PVFS_SID_get_server_first_n(NULL, *sid_array, &n, SID_SERVER_META);

    /* generate dirdata handles */
    *datafile_handles = (PVFS_OID *)malloc(datafile_count * sizeof(PVFS_OID));
    for (i = 0; i < datafile_count; i++)
    {
        PVFS_OID_gen((&(*datafile_handles)[i]));
    }

    /* generate SIDs for datafile objects */
    *datafile_sid_array = (PVFS_SID *)malloc(datafile_count *
                                             datafile_sid_count *
                                             sizeof(PVFS_SID));
    n = datafile_sid_count * datafile_count;
    PVFS_SID_get_server_next_n(NULL, *datafile_sid_array, &n, SID_SERVER_DATA);
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

    /* generate metadata handle */
    *handle = malloc(sizeof(PVFS_handle));
    PVFS_OID_gen(*handle);

    /* generate SIDs for metadata object */
    *sid_array = (PVFS_SID *)malloc(sid_count * sizeof(PVFS_SID));
    n = sid_count;
    PVFS_SID_get_server_first_n(NULL, *sid_array, &n, SID_SERVER_META);

    /* generate dirdata handles */
    *dirdata_handles = (PVFS_OID *)malloc(dirdata_count * sizeof(PVFS_OID));
    for (i = 0; i < dirdata_count; i++)
    {
        PVFS_OID_gen(*dirdata_handles);
    }

    /* generate SIDs for dirdata objects */
    *dirdata_sid_array = (PVFS_SID *)malloc(dirdata_count *
                                             dirdata_sid_count *
                                             sizeof(PVFS_SID));
    n = dirdata_sid_count;
    PVFS_SID_get_server_next_n(NULL, *dirdata_sid_array, &n, SID_SERVER_META);
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

