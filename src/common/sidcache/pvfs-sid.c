
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

enum {
    PVFS_OBJ_META,
    PVFS_OBJ_DATA
};

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
    ret = PVFS_OBJ_gen(obj, obj_count, fs_id, PVFS_OBJ_META);
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
    ret = PVFS_OBJ_gen(obj, obj_count, fs_id, PVFS_OBJ_DATA);
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
        memset(obj_array[i].sid_array, 0, sizeof(PVFS_SID) * num_copies);
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
    ret = SID_cache_lookup_server(&SID_db, sid, &temp_cacheval);
    if (ret != 0)
    {
        return ret;
    }
    if (temp_cacheval->bmi_addr == 0);
    {
#if 0
        /* enter url into BMI to get BMI addr */
        ret = BMI_addr_lookup(&temp_cacheval->bmi_addr, temp_cacheval->url);
        if (ret != 0)
        {
            return ret;
        }
        ret = SID_cache_update_server(&SID_db, sid, temp_cacheval);
        if (ret != 0)
        {
            return ret;
        }
#endif
    }
    *bmi_addr = temp_cacheval->bmi_addr;
    free(temp_cacheval);
    return ret;
}

static int PVFS_SID_get_server(PVFS_BMI_addr_t *bmi_addr,
                               uint32_t stype,
                               uint32_t flag)
{
    int ret = 0;
    DBT key, data;
    SID_cacheval_t *temp_cacheval;

    SID_zero_dbt(&key, &data, NULL);

    key.data = &stype;
    key.size = sizeof(uint32_t);
   
    ret = SID_type_cursor->get(SID_type_cursor, /* type index */
                               &key,            /* Sid_sid_t sid */
                               &data,           /* SID_cacheval_t struct ptr */
                               flag);           /* get flags */
    if(ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "Error getting sid from sid cache : %s\n",
                     db_strerror(ret));
        return(ret);
    }

    /* Unmarshalling the data */
    SID_cacheval_unpack(&temp_cacheval, &data);
    *bmi_addr = temp_cacheval->bmi_addr;
    free(temp_cacheval);
    return(ret);
}

int PVFS_SID_get_server_first(PVFS_BMI_addr_t *bmi_addr, uint32_t stype)
{
    return PVFS_SID_get_server(bmi_addr, stype, DB_SET);
}

int PVFS_SID_get_server_next(PVFS_BMI_addr_t *bmi_addr, uint32_t stype)
{
    return PVFS_SID_get_server(bmi_addr, stype, DB_NEXT);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

