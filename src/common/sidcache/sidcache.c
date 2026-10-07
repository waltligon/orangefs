/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 *
 * The function and variable names are the SID cache interface from
 * the Berkeley DB version of this file. Storage calls go through
 * sidcache-db.h, the same kind of layer Trove uses in dbpf-db.h.
 * The LMDB calls are in sidcache-db-lmdb.c. What this port changed,
 * and what is still unfinished, is at the top of sidcache.h.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pvfs2-internal.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "pvfs2-types.h"
#include "pvfs3-handle.h"
#include "bmi.h"
#include "sidcache.h"
#include "server-config.h"
#include "server-config-mgr.h"

/* Global number of records (SIDs) in the cache */
static int sids_in_cache = 0;

/* Global database variables */
sid_db *SID_db = NULL;                       /* Primary database (sid cache) */
sid_env *SID_envp = NULL;                    /* Env for sid cache and secondary dbs */
sid_db *SID_attr_index[SID_NUM_ATTR];        /* Array of secondary databases */
sid_cursor *SID_attr_cursor[SID_NUM_ATTR];   /* Array of secondary database cursors */
sid_db *SID_type_db = NULL;                  /* db for server type */
sid_cursor *SID_type_cursor = NULL;          /* cursor for server type db */
sid_db *SID_type_index = NULL;               /* index on sid for server type db */
sid_cursor *SID_index_cursor = NULL;         /* cursor for server type sid index */
void *SID_txn = NULL;                        /* was DB_TXN *; stays NULL */
struct sid_data bulk_next_key;               /* resume key for a bulk read */

/* NOTE: SID_type_db and SID_type_index are plain databases. They are not
 * secondary indexes maintained by the database library. Records inserted
 * or removed in one are inserted or removed in the other by
 * SID_cache_update_type_single and SID_cache_delete_server.
 */

static char sid_dir[64];

struct type_conv
{
    uint32_t typeval;
    char *typestring;
} type_conv_table[] =
{
    {SID_SERVER_ROOT        , "ROOT"} ,
    {SID_SERVER_PRIME       , "PRIME"} ,
    {SID_SERVER_CONFIG      , "CONFIG"} ,
    {SID_SERVER_LOCAL       , "LOCAL"} ,
    {SID_SERVER_META        , "META"} ,
    {SID_SERVER_DATA        , "DATA"} ,
    {SID_SERVER_DIRM        , "DIR"} ,
    {SID_SERVER_DIRD        , "DIRDATA"} ,
    {SID_SERVER_SECURITY    , "SECURITY"} ,
    {SID_SERVER_ME          , "ME"} ,
    {SID_SERVER_VALID_TYPES , "ALL"} ,
    {SID_SERVER_NULL        , "INVALID"}
};

#define MAX_TYPE_STR 8

static int SID_initialize_secondary_dbs(sid_db *secondary_dbs[]);
static int SID_cache_update_type(const PVFS_SID *sid_server,
                                 struct SID_type_s *new_type_val);
static int SID_cache_update_type_single(const PVFS_SID *sid_server,
                                        uint32_t new_type_val,
                                        PVFS_fs_id fsid);
static int SID_cache_update_attrs(sid_db *dbp,
                                  const PVFS_SID *sid_server,
                                  int new_attr[]);
static int SID_create_type_table(void);
static char *SID_type_to_string(char *buf, struct SID_type_s typeval, int n);

static void sid_reset_dir(const char *dir)
{
    char path[80];

    snprintf(path, sizeof(path), "%s/data.mdb", dir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/lock.mdb", dir);
    unlink(path);
    rmdir(dir);
    mkdir(dir, 0700);
}

static int SID_initialize_secondary_dbs(sid_db *secondary_dbs[])
{
    memset(secondary_dbs, 0, sizeof(sid_db *) * SID_NUM_ATTR);
    return 0;
}

void SID_zero_dbt(struct sid_data *key,
                  struct sid_data *data,
                  struct sid_data *pkey)
{
    if (key)
    {
        memset(key, 0, sizeof(*key));
    }
    if (data)
    {
        memset(data, 0, sizeof(*data));
    }
    if (pkey)
    {
        memset(pkey, 0, sizeof(*pkey));
    }
}

static char *SID_type_to_string(char *buf, struct SID_type_s typeval, int n)
{
    int i;

    for (i = 0;
         type_conv_table[i].typeval != typeval.server_type &&
         type_conv_table[i].typeval != SID_SERVER_NULL;
         i++)
    {
    }
    if (typeval.fsid != 0)
    {
        if (n < MAX_TYPE_STR + 12)
        {
            return NULL;
        }
        snprintf(buf, n, "%s(%d) ", type_conv_table[i].typestring, typeval.fsid);
    }
    else
    {
        if (n < MAX_TYPE_STR + 2)
        {
            return NULL;
        }
        snprintf(buf, n, "%s ", type_conv_table[i].typestring);
    }
    return buf;
}

int SID_string_to_type(const char *typestring)
{
    int i;
    char *mytype;
    int len;

    if (!typestring)
    {
        return -PVFS_EINVAL;
    }
    len = (int)strnlen(typestring, MAX_TYPE_STR + 1);
    if (len > MAX_TYPE_STR)
    {
        return -PVFS_EINVAL;
    }
    mytype = (char *)malloc(len + 1);
    if (!mytype)
    {
        return -PVFS_ENOMEM;
    }
    for (i = 0; i < len; i++)
    {
        mytype[i] = toupper((unsigned char)typestring[i]);
    }
    mytype[len] = 0;
    for (i = 0;
         strncmp(type_conv_table[i].typestring, mytype, MAX_TYPE_STR) &&
         type_conv_table[i].typeval != SID_SERVER_NULL;
         i++)
    {
    }
    free(mytype);
    if (type_conv_table[i].typeval == SID_SERVER_NULL)
    {
        return -PVFS_EINVAL;
    }
    return (int)type_conv_table[i].typeval;
}

int SID_set_attr(const char *attr_str, int **attributes)
{
    int i = 0;
    int len = 0;
    int eqflag = 0;
    int atflag = 0;
    char *myattr = NULL;
    char *myval = NULL;

    if (!attributes || !attr_str)
    {
        return -1;
    }
    len = (int)strnlen(attr_str, MAX_ATTR_STR);
    if (len > MAX_ATTR_STR || len == MAX_ATTR_STR)
    {
        return -1;
    }
    myattr = (char *)malloc(len + 1);
    if (!myattr)
    {
        return -1;
    }
    for (i = 0; i < len; i++)
    {
        if (attr_str[i] == '=')
        {
            myattr[i] = 0;
            if (i == len - 1)
            {
                free(myattr);
                return -1;
            }
            myval = &myattr[i + 1];
            eqflag = 1;
        }
        else
        {
            myattr[i] = tolower((unsigned char)attr_str[i]);
        }
    }
    myattr[len] = 0;
    if (!eqflag)
    {
        free(myattr);
        return -1;
    }
    if (!*attributes)
    {
        *attributes = (int *)malloc(SID_NUM_ATTR * sizeof(int));
        if (!*attributes)
        {
            free(myattr);
            return -1;
        }
        memset(*attributes, -1, SID_NUM_ATTR * sizeof(int));
        atflag = 1;
    }
    for (i = 0; i < SID_NUM_ATTR; i++)
    {
        if (!strcmp(myattr, SID_attr_map[i]))
        {
            (*attributes)[i] = atoi(myval);
            free(myattr);
            return 0;
        }
    }
    if (atflag)
    {
        free(*attributes);
        *attributes = NULL;
    }
    free(myattr);
    return -1;
}

void SID_cacheval_init(SID_cacheval_t **cacheval)
{
    memset((*cacheval)->attr, -1, sizeof(int) * SID_NUM_ATTR);
    memset(&((*cacheval)->bmi_addr), 0, sizeof(BMI_addr));
    (*cacheval)->url = (char *)&(*cacheval)[1];
    (*cacheval)->url[0] = 0;
}

void SID_cacheval_pack(const SID_cacheval_t *cacheval, struct sid_data *val)
{
    val->data = (void *)cacheval;
    val->len = sizeof(*cacheval) + strlen(cacheval->url) + 1;
}

int SID_cacheval_alloc(SID_cacheval_t **cacheval,
                       const int sid_attributes[],
                       const BMI_addr sid_bmi,
                       const char *sid_url)
{
    int ulen;

    if (!sid_url)
    {
        *cacheval = NULL;
        return -1;
    }
    ulen = (int)strlen(sid_url) + 1;
    *cacheval = (SID_cacheval_t *)malloc(sizeof(SID_cacheval_t) + ulen);
    if (!*cacheval)
    {
        return -PVFS_ENOMEM;
    }
    SID_cacheval_init(cacheval);
    if (sid_attributes)
    {
        memcpy((*cacheval)->attr, sid_attributes, sizeof(int) * SID_NUM_ATTR);
    }
    (*cacheval)->bmi_addr = sid_bmi;
    memcpy((*cacheval)->url, sid_url, ulen);
    return 0;
}

void SID_cacheval_free(SID_cacheval_t **cacheval)
{
    if (!cacheval || !*cacheval)
    {
        return;
    }
    free(*cacheval);
    *cacheval = NULL;
}

void SID_cacheval_unpack(SID_cacheval_t **cacheval, struct sid_data *data)
{
    SID_cacheval_t *copy;

    if (!cacheval || !data || !data->data ||
        data->len < sizeof(SID_cacheval_t))
    {
        if (cacheval)
        {
            *cacheval = NULL;
        }
        return;
    }
    copy = (SID_cacheval_t *)malloc(data->len);
    if (!copy)
    {
        *cacheval = NULL;
        return;
    }
    memcpy(copy, data->data, data->len);
    copy->url = (char *)&copy[1];
    *cacheval = copy;
}

int SID_cache_put(sid_db *dbp,
                  const PVFS_SID *sid_server,
                  const SID_cacheval_t *cacheval,
                  int *num_db_records)
{
    struct sid_data key;
    struct sid_data val;
    struct sid_data oldval;
    SID_cacheval_t *old = NULL;
    int existed = 0;
    int rc;
    int i;

    if (!dbp || !SID_envp || !cacheval || !cacheval->url ||
        PVFS_SID_is_null(sid_server))
    {
        return -PVFS_EINVAL;
    }

    SID_zero_dbt(&key, &val, NULL);
    key.data = (void *)sid_server;
    key.len = sizeof(*sid_server);
    SID_cacheval_pack(cacheval, &val);

    rc = sid_db_txn_begin(SID_envp, 0);
    if (rc)
    {
        return rc;
    }
    SID_zero_dbt(&oldval, NULL, NULL);
    rc = sid_db_get(dbp, &key, &oldval);
    if (rc == 0)
    {
        existed = 1;
        if (num_db_records != NULL)
        {
            free(oldval.data);
            sid_db_txn_abort(SID_envp);
            return 0;
        }
        SID_cacheval_unpack(&old, &oldval);
        free(oldval.data);
        if (!old)
        {
            sid_db_txn_abort(SID_envp);
            return -PVFS_ENOMEM;
        }
    }
    else if (rc != -PVFS_ENOENT)
    {
        sid_db_txn_abort(SID_envp);
        return rc;
    }

    rc = sid_db_put(dbp, &key, &val);
    if (rc)
    {
        SID_cacheval_free(&old);
        sid_db_txn_abort(SID_envp);
        return rc;
    }
    for (i = 0; i < SID_NUM_ATTR; i++)
    {
        int32_t new_key = 0;
        struct sid_data pkey;
        struct sid_data pdata;
        struct sid_data skey;
        struct sid_data akey;
        struct sid_data aval;

        /* Same call DB->associate used to make. skey points into pdata. */
        SID_zero_dbt(&pkey, &pdata, &skey);
        pkey.data = (void *)sid_server;
        pkey.len = sizeof(*sid_server);
        SID_cacheval_pack(cacheval, &pdata);
        if (SID_extract_key[i](dbp, &pkey, &pdata, &skey) != 0 ||
            !skey.data || skey.len < sizeof(new_key))
        {
            new_key = cacheval->attr[i];
        }
        else
        {
            memcpy(&new_key, skey.data, sizeof(new_key));
        }
        if (old && old->attr[i] == new_key)
        {
            continue;
        }
        SID_zero_dbt(&akey, &aval, NULL);
        aval.data = (void *)sid_server;
        aval.len = sizeof(*sid_server);
        if (old)
        {
            int32_t old_key = old->attr[i];
            akey.data = &old_key;
            akey.len = sizeof(old_key);
            rc = sid_db_del(SID_attr_index[i], &akey, &aval);
            if (rc && rc != -PVFS_ENOENT)
            {
                SID_cacheval_free(&old);
                sid_db_txn_abort(SID_envp);
                return rc;
            }
        }
        akey.data = &new_key;
        akey.len = sizeof(new_key);
        rc = sid_db_putdup(SID_attr_index[i], &akey, &aval);
        if (rc)
        {
            SID_cacheval_free(&old);
            sid_db_txn_abort(SID_envp);
            return rc;
        }
    }
    SID_cacheval_free(&old);
    rc = sid_db_txn_commit(SID_envp);
    if (rc)
    {
        return rc;
    }
    if (!existed)
    {
        sids_in_cache++;
    }
    if (num_db_records != NULL)
    {
        *num_db_records = sids_in_cache;
    }
    return 0;
}

int SID_cache_get(sid_db *dbp,
                  const PVFS_SID *sid_server,
                  SID_cacheval_t **cacheval)
{
    struct sid_data key;
    struct sid_data val;
    int rc;

    if (!dbp || !sid_server || !cacheval)
    {
        return -PVFS_EINVAL;
    }
    *cacheval = NULL;
    SID_zero_dbt(&key, &val, NULL);
    key.data = (void *)sid_server;
    key.len = sizeof(*sid_server);
    rc = sid_db_get(dbp, &key, &val);
    if (rc)
    {
        return rc;
    }
    SID_cacheval_unpack(cacheval, &val);
    free(val.data);
    if (!*cacheval)
    {
        return -PVFS_ENOMEM;
    }
    return 0;
}

int SID_cache_lookup_bmi(sid_db *dbp, const PVFS_SID *search_sid, char **bmi_url)
{
    SID_cacheval_t *temp = NULL;
    int ret;

    ret = SID_cache_get(dbp, search_sid, &temp);
    if (ret)
    {
        return ret;
    }
    *bmi_url = malloc(strlen(temp->url) + 1);
    if (!*bmi_url)
    {
        SID_cacheval_free(&temp);
        return -PVFS_ENOMEM;
    }
    strcpy(*bmi_url, temp->url);
    SID_cacheval_free(&temp);
    return 0;
}

int SID_cache_copy_attrs(SID_cacheval_t *current_attrs, int new_attr[])
{
    if (!current_attrs || !new_attr)
    {
        return -PVFS_EINVAL;
    }
    memcpy(current_attrs->attr, new_attr, sizeof(int) * SID_NUM_ATTR);
    return 0;
}

int SID_cache_copy_bmi(SID_cacheval_t *current_attrs, BMI_addr new_bmi_addr)
{
    if (!current_attrs)
    {
        return -PVFS_EINVAL;
    }
    current_attrs->bmi_addr = new_bmi_addr;
    return 0;
}

int SID_cache_copy_url(SID_cacheval_t **current_attrs, char *new_url)
{
    SID_cacheval_t *grown;
    size_t ulen;

    if (!current_attrs || !*current_attrs || !new_url)
    {
        return -PVFS_EINVAL;
    }
    ulen = strlen(new_url) + 1;
    grown = (SID_cacheval_t *)malloc(sizeof(SID_cacheval_t) + ulen);
    if (!grown)
    {
        return -PVFS_ENOMEM;
    }
    memcpy(grown, *current_attrs, sizeof(SID_cacheval_t));
    grown->url = (char *)&grown[1];
    memcpy(grown->url, new_url, ulen);
    free(*current_attrs);
    *current_attrs = grown;
    return 0;
}

static int SID_cache_update_attrs(sid_db *dbp,
                                  const PVFS_SID *sid_server,
                                  int new_attr[])
{
    SID_cacheval_t *cur = NULL;
    int rc;

    rc = SID_cache_get(dbp, sid_server, &cur);
    if (rc)
    {
        return rc;
    }
    SID_cache_copy_attrs(cur, new_attr);
    rc = SID_cache_put(dbp, sid_server, cur, NULL);
    SID_cacheval_free(&cur);
    return rc;
}

int SID_cache_update_bmi(sid_db *dbp,
                         const PVFS_SID *sid_server,
                         BMI_addr new_bmi_addr)
{
    SID_cacheval_t *cur = NULL;
    int rc;

    rc = SID_cache_get(dbp, sid_server, &cur);
    if (rc)
    {
        return rc;
    }
    SID_cache_copy_bmi(cur, new_bmi_addr);
    rc = SID_cache_put(dbp, sid_server, cur, NULL);
    SID_cacheval_free(&cur);
    return rc;
}

int SID_cache_update_url(sid_db *dbp, const PVFS_SID *sid_server, char *new_url)
{
    SID_cacheval_t *cur = NULL;
    int rc;

    rc = SID_cache_get(dbp, sid_server, &cur);
    if (rc)
    {
        return rc;
    }
    rc = SID_cache_copy_url(&cur, new_url);
    if (rc)
    {
        SID_cacheval_free(&cur);
        return rc;
    }
    rc = SID_cache_put(dbp, sid_server, cur, NULL);
    SID_cacheval_free(&cur);
    return rc;
}

int SID_cache_update_server(sid_db *dbp,
                            const PVFS_SID *sid_server,
                            SID_cacheval_t *new_attrs,
                            struct SID_type_s *sid_types)
{
    int rc;

    if (!new_attrs)
    {
        return -PVFS_EINVAL;
    }
    rc = SID_cache_put(dbp, sid_server, new_attrs, NULL);
    if (rc)
    {
        return rc;
    }
    if (sid_types)
    {
        rc = SID_cache_update_type(sid_server, sid_types);
    }
    return rc;
}

static int SID_cache_update_type_single(const PVFS_SID *sid_server,
                                        uint32_t new_type_val,
                                        PVFS_fs_id fsid)
{
    struct SID_type_s type;
    struct sid_data key;
    struct sid_data val;
    int rc;

    if (!SID_type_db || !SID_type_index || !sid_server)
    {
        return -PVFS_EINVAL;
    }
    if ((new_type_val & SID_SERVER_VALID_TYPES) == 0)
    {
        return 0;
    }
    type.fsid = fsid;
    type.server_type = new_type_val;
    SID_zero_dbt(&key, &val, NULL);
    key.data = &type;
    key.len = sizeof(type);
    val.data = (void *)sid_server;
    val.len = sizeof(*sid_server);
    rc = sid_db_txn_begin(SID_envp, 0);
    if (rc)
    {
        return rc;
    }
    rc = sid_db_putdup(SID_type_db, &key, &val);
    if (rc)
    {
        sid_db_txn_abort(SID_envp);
        return rc;
    }
    key.data = (void *)sid_server;
    key.len = sizeof(*sid_server);
    val.data = &type;
    val.len = sizeof(type);
    rc = sid_db_putdup(SID_type_index, &key, &val);
    if (rc)
    {
        sid_db_txn_abort(SID_envp);
        return rc;
    }
    return sid_db_txn_commit(SID_envp);
}

static int SID_cache_update_type(const PVFS_SID *sid_server,
                                 struct SID_type_s *new_type_val)
{
    uint32_t mask;
    uint32_t type_val;
    int rc = 0;

    if (!sid_server || !new_type_val)
    {
        return -PVFS_EINVAL;
    }
    type_val = new_type_val->server_type;
    for (mask = 1; mask != 0 && type_val != 0; mask <<= 1)
    {
        if (type_val & mask)
        {
            if (mask & SID_SERVER_VALID_TYPES)
            {
                rc = SID_cache_update_type_single(sid_server, mask,
                                                  new_type_val->fsid);
                if (rc)
                {
                    return rc;
                }
            }
            type_val &= ~mask;
        }
    }
    return rc;
}

int SID_cache_delete_server(sid_db *dbp,
                            const PVFS_SID *sid_server,
                            int *db_records)
{
    struct sid_data key;
    struct sid_data val;
    SID_cacheval_t *old = NULL;
    struct SID_type_s *types = NULL;
    int ntypes = 0;
    sid_cursor *cursorp = NULL;
    int rc;
    int i;

    if (!dbp || !SID_envp || !sid_server)
    {
        return -PVFS_EINVAL;
    }
    rc = sid_db_txn_begin(SID_envp, 0);
    if (rc)
    {
        return rc;
    }
    SID_zero_dbt(&key, &val, NULL);
    key.data = (void *)sid_server;
    key.len = sizeof(*sid_server);
    rc = sid_db_get(dbp, &key, &val);
    if (rc)
    {
        sid_db_txn_abort(SID_envp);
        return rc;
    }
    SID_cacheval_unpack(&old, &val);
    free(val.data);
    if (!old)
    {
        sid_db_txn_abort(SID_envp);
        return -PVFS_ENOMEM;
    }
    for (i = 0; i < SID_NUM_ATTR; i++)
    {
        int32_t attr = old->attr[i];
        struct sid_data akey;
        struct sid_data aval;

        SID_zero_dbt(&akey, &aval, NULL);
        akey.data = &attr;
        akey.len = sizeof(attr);
        aval.data = (void *)sid_server;
        aval.len = sizeof(*sid_server);
        rc = sid_db_del(SID_attr_index[i], &akey, &aval);
        if (rc && rc != -PVFS_ENOENT)
        {
            SID_cacheval_free(&old);
            sid_db_txn_abort(SID_envp);
            return rc;
        }
    }
    SID_cacheval_free(&old);

    rc = sid_db_cursor(SID_type_index, &cursorp, 0);
    if (rc)
    {
        sid_db_txn_abort(SID_envp);
        return rc;
    }
    SID_zero_dbt(&key, &val, NULL);
    key.data = (void *)sid_server;
    key.len = sizeof(*sid_server);
    rc = sid_db_cursor_get(cursorp, &key, &val, SID_DB_CURSOR_SET);
    while (rc == 0)
    {
        struct SID_type_s *grown;
        if (val.len == sizeof(struct SID_type_s))
        {
            grown = (struct SID_type_s *)realloc(
                types, (ntypes + 1) * sizeof(*types));
            if (!grown)
            {
                free(val.data);
                free(types);
                sid_db_cursor_close(cursorp);
                sid_db_txn_abort(SID_envp);
                return -PVFS_ENOMEM;
            }
            types = grown;
            memcpy(&types[ntypes], val.data, sizeof(*types));
            ntypes++;
        }
        free(val.data);
        val.data = NULL;
        rc = sid_db_cursor_get(cursorp, &key, &val, SID_DB_CURSOR_NEXT_DUP);
    }
    sid_db_cursor_close(cursorp);
    if (rc != -PVFS_ENOENT && rc != 0)
    {
        free(types);
        sid_db_txn_abort(SID_envp);
        return rc;
    }
    for (i = 0; i < ntypes; i++)
    {
        struct sid_data tkey;
        struct sid_data tval;

        SID_zero_dbt(&tkey, &tval, NULL);
        tkey.data = &types[i];
        tkey.len = sizeof(types[i]);
        tval.data = (void *)sid_server;
        tval.len = sizeof(*sid_server);
        rc = sid_db_del(SID_type_db, &tkey, &tval);
        if (rc && rc != -PVFS_ENOENT)
        {
            free(types);
            sid_db_txn_abort(SID_envp);
            return rc;
        }
        tkey.data = (void *)sid_server;
        tkey.len = sizeof(*sid_server);
        tval.data = &types[i];
        tval.len = sizeof(types[i]);
        rc = sid_db_del(SID_type_index, &tkey, &tval);
        if (rc && rc != -PVFS_ENOENT)
        {
            free(types);
            sid_db_txn_abort(SID_envp);
            return rc;
        }
    }
    free(types);
    SID_zero_dbt(&key, NULL, NULL);
    key.data = (void *)sid_server;
    key.len = sizeof(*sid_server);
    rc = sid_db_del(dbp, &key, NULL);
    if (rc)
    {
        sid_db_txn_abort(SID_envp);
        return rc;
    }
    rc = sid_db_txn_commit(SID_envp);
    if (rc)
    {
        return rc;
    }
    if (sids_in_cache > 0)
    {
        sids_in_cache--;
    }
    if (db_records)
    {
        *db_records = sids_in_cache;
    }
    return 0;
}

int SID_get_type(PVFS_SID *sid, uint32_t *typeval)
{
    sid_cursor *cursorp = NULL;
    struct sid_data key;
    struct sid_data val;
    int rc;

    if (!sid || !typeval || !SID_type_index)
    {
        return -PVFS_EINVAL;
    }
    *typeval = 0;
    rc = sid_db_cursor(SID_type_index, &cursorp, 1);
    if (rc)
    {
        return rc;
    }
    SID_zero_dbt(&key, &val, NULL);
    key.data = sid;
    key.len = sizeof(*sid);
    rc = sid_db_cursor_get(cursorp, &key, &val, SID_DB_CURSOR_SET);
    while (rc == 0)
    {
        if (val.len == sizeof(struct SID_type_s))
        {
            struct SID_type_s type;
            memcpy(&type, val.data, sizeof(type));
            *typeval |= type.server_type;
        }
        free(val.data);
        val.data = NULL;
        rc = sid_db_cursor_get(cursorp, &key, &val, SID_DB_CURSOR_NEXT_DUP);
    }
    sid_db_cursor_close(cursorp);
    if (rc != -PVFS_ENOENT && rc != 0)
    {
        return rc;
    }
    return 0;
}

/* The loader reads the ATTRS header. The writer emits <ServerDefines>.
 * Those two texts are the ones this file had. The #if 0 blocks are the
 * older numeric dump, left disabled.
 *
 * V3 NEEDS TO BE DONE: type lines do not carry an fsid yet. Loaded
 * types apply to every file system (fsid 0).
 */
static int SID_cache_parse_header(FILE *inpfile,
                                  int *records_in_file,
                                  int *attrs_in_file,
                                  int **attr_positions)
{
    int i = 0;
    int j = 0;
    char **attrs_strings;
    char tmp_buff[TMP_BUFF_SIZE];

    /* Checking to make sure the input file is open, the function
     * load_sid_cache_from_file should have opened the file
     */
    if (!inpfile || !records_in_file || !attrs_in_file || !attr_positions)
    {
        gossip_err("File is not opened. Exiting load_sid_cache_from_file\n");
        return -1;
    }
    *attr_positions = NULL;
    memset(tmp_buff, 0, sizeof(tmp_buff));

    /* Getting the total number of attributes from the file */
    if (fscanf(inpfile, "%s", tmp_buff) != 1)
    {
        return -1;
    }
    if (fscanf(inpfile, "%d", attrs_in_file) != 1)
    {
        return -1;
    }
    if (*attrs_in_file > SID_NUM_ATTR || *attrs_in_file < 1)
    {
        gossip_err("The number of attributes in the input file "
                   "was not within the proper range\n");
        gossip_err("The contents of the database will not be read "
                   "from the inputfile\n");
        return -1;
    }

    /* Getting the number of sids in from the file */
    if (fscanf(inpfile, "%s", tmp_buff) != 1)
    {
        return -1;
    }
    if (fscanf(inpfile, "%d", records_in_file) != 1)
    {
        return -1;
    }

    /* Checking to make sure the input file has the number of sids as the
     *  entry in the input file
     */
    if (*records_in_file == 0)
    {
        gossip_err("%s: There are no sids in the input file\n", __func__);
        return -1;
    }

    /* Mallocing space to hold the name of the attributes in the file
     * and initializing the attributes string array
     */
    attrs_strings = (char **)malloc(sizeof(char *) * *attrs_in_file);
    if (!attrs_strings)
    {
        gossip_err("%s: malloc attr strings failed\n", __func__);
        return -1;
    }
    memset(attrs_strings, 0, sizeof(char *) * *attrs_in_file);

    /* Mallocing space to hold the positions of the attributes in the file for
     * the cacheval_t attribute arrays and initializing the position array
     */
    *attr_positions = (int *)malloc(sizeof(int) * *attrs_in_file);
    if (!*attr_positions)
    {
        gossip_err("%s: malloc attr positions failed\n", __func__);
        free(attrs_strings);
        return -1;
    }
    memset(*attr_positions, 0, sizeof(int) * *attrs_in_file);

    /* Getting the attribute strings from the input file */
    for (i = 0; i < *attrs_in_file; i++)
    {
        int slen = 0;

        if (fscanf(inpfile, "%s", tmp_buff) != 1)
        {
            goto fail;
        }
        slen = (int)strlen(tmp_buff) + 1;
        attrs_strings[i] = (char *)malloc((size_t)slen);
        if (!attrs_strings[i])
        {
            gossip_err("%s: malloc attr strings %d failed\n", __func__, i);
            goto fail;
        }
        /* we already know the length of the string and buffer so
         * do a memcpy, not a strcpy
         */
        memcpy(attrs_strings[i], tmp_buff, (size_t)slen);
    }

    /* Getting the correct positions from the input file for the
     * attributes in the SID_cacheval_t attrs array
     */
    for (i = 0; i < *attrs_in_file; i++)
    {
        for (j = 0; j < SID_NUM_ATTR; j++)
        {
            if (!strcmp(attrs_strings[i], SID_attr_map[j]))
            {
                (*attr_positions)[i] = j;
                break;
            }
        }
        /* If the attribute string was not a valid option it will be ignored
         * and not added to the sid cache
         */
        if (j == SID_NUM_ATTR)
        {
            gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                         "Attribute: %s is an invalid attribute, "
                         "and it will not be added\n",
                         attrs_strings[i]);
            (*attr_positions)[i] = -1;
        }
    }

    /* Freeing the dynamically allocated memory */
    for (i = 0; i < *attrs_in_file; i++)
    {
        free(attrs_strings[i]);
    }
    free(attrs_strings);
    return 0;

fail:
    for (i = 0; i < *attrs_in_file; i++)
    {
        free(attrs_strings[i]);
    }
    free(attrs_strings);
    free(*attr_positions);
    *attr_positions = NULL;
    return -1;
}

/** SID_type_load
 * This function reads from a file and locates type identifiers
 * Each one is added to the type_db
 * Invalid items or EOL cause the scan to stop
 */
#define TYPELINELEN 1024
static int SID_type_load(FILE *inpfile, const PVFS_SID *sid)
{
    int ret = 0;
    struct sid_data type_key;
    struct sid_data type_val;
    struct sid_data index_key;
    struct sid_data index_val;
    char linebuff[TYPELINELEN];
    char *lineptr = linebuff;
    char *saveptr = NULL;
    char *typeword = NULL;
    PVFS_SID sid_buf;
    struct SID_type_s type_buf;

    if (!inpfile || !sid || !SID_type_db || !SID_type_index)
    {
        return -1;
    }
    sid_buf = *sid;
    SID_zero_dbt(&type_key, &type_val, NULL);
    SID_zero_dbt(&index_key, &index_val, NULL);
    type_key.data = &type_buf;
    type_key.len = sizeof(struct SID_type_s);
    type_val.data = &sid_buf;
    type_val.len = sizeof(sid_buf);
    index_key.data = &sid_buf;
    index_key.len = sizeof(sid_buf);
    index_val.data = &type_buf;
    index_val.len = sizeof(struct SID_type_s);

    /* read a line */
    memset(linebuff, 0, TYPELINELEN);
    if (!fgets(linebuff, TYPELINELEN, inpfile))
    {
        return 0;
    }

    while (1)
    {
        /* read next word */
        /* strtok is not reentrant, strtok_r is not standard (gcc
         * extension) need to add config support to select the right
         * approach - V3
         * NOTE: strtok_r is POSIX 2001
         */
# if 1
        typeword = strtok_r(lineptr, " \t\n", &saveptr);
# else
        typeword = strtok(lineptr, " \t\n");
# endif
        if (!typeword)
        {
            return 0; /* no more type tokens */
        }
        lineptr = NULL; /* subsequent calls from saved string pointer */

        /* V3 NEEDS TO BE DONE */
        /* check for an fs_id and set in buffer */
        type_buf.fsid = 0; /* applies to all fs */

        /* a zero return is an invalid typeval */
        if (0 < (ret = SID_string_to_type(typeword)))
        {
            type_buf.server_type = (uint32_t)ret;
            ret = sid_db_txn_begin(SID_envp, 0);
            if (ret)
            {
                break; /* DB error */
            }
            /* insert into type database */
            ret = sid_db_putdup(SID_type_db, &type_key, &type_val);
            if (ret)
            {
                sid_db_txn_abort(SID_envp);
                break; /* DB error */
            }
            /* insert into type index database */
            ret = sid_db_putdup(SID_type_index, &index_key, &index_val);
            if (ret)
            {
                sid_db_txn_abort(SID_envp);
                break; /* DB error */
            }
            ret = sid_db_txn_commit(SID_envp);
            if (ret)
            {
                break; /* DB error */
            }
        }
        else
        {
            break; /* invalid type string or error in string-to-type */
        }
    }
    return ret;
}
#undef TYPELINELEN

/** SID_LOAD
 *
 * This function loads the contents of an input file into the cache.
 * SID records in the file are added to the current contents of the
 * cache.  Duplicates are rejected with an error, only the first value
 * is kept.
 *
 * Returns 0 on success, otherwise returns an error code
 * The number of sids in the file is returned through the
 * parameter db_records.
 */
int SID_cache_load(sid_db *dbp, FILE *inpfile, int *num_db_records)
{
    int ret = 0;
    int i = 0;
    int *attr_positions = NULL;
    int attr_pos_index = 0;
    int attrs_in_file = 0;
    int records_in_file = 0;
    int sid_attributes[SID_NUM_ATTR];
    int throw_away_attr = 0;
    BMI_addr tmp_bmi = 0;
    long bmi_scan = 0;
    char tmp_url[TMP_BUFF_SIZE];
    char tmp_sid_str[SID_STR_LEN];
    PVFS_SID current_sid;
    SID_cacheval_t *current_sid_cacheval = NULL;

    if (!dbp || !inpfile)
    {
        return -PVFS_EINVAL;
    }

    /* Getting the attributes from the input file */
    ret = SID_cache_parse_header(inpfile,
                                 &records_in_file,
                                 &attrs_in_file,
                                 &attr_positions);
    if (ret)
    {
        return ret;
    }

    for (i = 0; i < records_in_file; i++)
    {
        /* Read the sid's string representation from the input file */
        if (fscanf(inpfile, "%s", tmp_sid_str) != 1)
        {
            ret = -PVFS_EINVAL;
            break;
        }

        /* convert to binary */
        ret = PVFS_SID_str2bin(tmp_sid_str, &current_sid);
        if (ret)
        {
            /* Skips adding sid to sid cache */
            gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                         "Error parsing PVFS_SID in "
                         "SID_load_cache_from_file function\n");
            continue;
        }

        /* Read the bmi address */
        if (fscanf(inpfile, SCANF_lld, &bmi_scan) != 1)
        {
            ret = -PVFS_EINVAL;
            break;
        }
        tmp_bmi = (BMI_addr)bmi_scan;

        /* Read the url */
        if (fscanf(inpfile, "%s", tmp_url) != 1)
        {
            ret = -PVFS_EINVAL;
            break;
        }

        /* Initializing the temporary attribute array */
        /* so all index's are currently -1 */
        memset(sid_attributes, -1, sizeof(int) * SID_NUM_ATTR);

        /* Read the attributes from the input file and place them in the
         * correct position in the attrs array in the SID_cacheval_t struct
         */
        for (attr_pos_index = 0;
             attr_pos_index < attrs_in_file;
             attr_pos_index++)
        {
            if (attr_positions[attr_pos_index] != -1)
            {
                if (fscanf(inpfile, "%d",
                           &(sid_attributes[attr_positions[attr_pos_index]])) != 1)
                {
                    ret = -PVFS_EINVAL;
                    break;
                }
            }
            else
            {
                if (fscanf(inpfile, "%d", &throw_away_attr) != 1)
                {
                    ret = -PVFS_EINVAL;
                    break;
                }
            }
        }
        if (ret)
        {
            break;
        }

        /* Read the type indicators from the file */
        SID_type_load(inpfile, &current_sid);

        /* This allocates and fills in the cacheval */
        ret = SID_cacheval_alloc(&current_sid_cacheval,
                                 sid_attributes,
                                 tmp_bmi,
                                 tmp_url);
        if (ret)
        {
            /* Presumably out of memory */
            break;
        }

        /* Storing the current sid from the input file into the sid cache.
         * num_db_records non-NULL keeps the first value of a duplicate.
         */
        ret = SID_cache_put(dbp,
                            &current_sid,
                            current_sid_cacheval,
                            num_db_records);
        SID_cacheval_free(&current_sid_cacheval);
        if (ret)
        {
            /* Need to examine error and decide if we should continue or
             * not - for now we stop
             */
            break;
        }
    }

    free(attr_positions);
    return ret;
}

/* loop over records matching sid and write type data */
int SID_type_store(PVFS_SID *sid, FILE *outpfile)
{
    int ret = 0;
    int opened = 0;
    struct sid_data type_sid_key;
    struct sid_data type_sid_val;
    char buff[20];
    PVFS_SID kbuf;
    struct SID_type_s tbuf;

    if (!sid || !outpfile || !SID_type_index)
    {
        return -1;
    }
    memset(&tbuf, 0, sizeof(tbuf));
    kbuf = *sid;
    SID_zero_dbt(&type_sid_key, &type_sid_val, NULL);
    type_sid_key.data = &kbuf;
    type_sid_key.len = sizeof(PVFS_SID);

    if (!SID_index_cursor)
    {
        ret = sid_db_cursor(SID_type_index, &SID_index_cursor, 1);
        if (ret)
        {
            return ret;
        }
        opened = 1;
    }

    ret = sid_db_cursor_get(SID_index_cursor,
                            &type_sid_key,
                            &type_sid_val,
                            SID_DB_CURSOR_SET);

    /* halt on error or not found. The old check was DB_NOTFOUND. */
    if (ret == 0 || ret == -PVFS_ENOENT)
    {
        fprintf(outpfile, "\t\tType ");
    }
    else
    {
        fprintf(outpfile, "ERROR not found finding types\n");
        if (opened)
        {
            sid_db_cursor_close(SID_index_cursor);
            SID_index_cursor = NULL;
        }
        return ret;
    }
    while (ret == 0)
    {
        if (type_sid_val.data &&
            type_sid_val.len == sizeof(struct SID_type_s))
        {
            memcpy(&tbuf, type_sid_val.data, sizeof(tbuf));
        }
        free(type_sid_val.data);
        type_sid_val.data = NULL;
        type_sid_val.len = 0;
        /* write type to file */
        if (SID_type_to_string(buff, tbuf, 20))
        {
            fprintf(outpfile, "%s ", buff);
        }
        tbuf.server_type = 0;
        tbuf.fsid = 0;
        ret = sid_db_cursor_get(SID_index_cursor,
                                &type_sid_key,
                                &type_sid_val,
                                SID_DB_CURSOR_NEXT_DUP);
    }

    fprintf(outpfile, "\n");
    if (opened)
    {
        sid_db_cursor_close(SID_index_cursor);
        SID_index_cursor = NULL;
    }
    /* Normal return should be not-found, 0 is no error */
    if (ret != 0 && ret != -PVFS_ENOENT)
    {
        fprintf(outpfile, "ERROR not found saving types\n");
        return ret;
    }
    return 0;
}

/** SID_STORE
 *
 * This function writes the contents of the sid cache in ASCII to the file
 * specified through the outpfile parameter parameter
 *
 * Returns 0 on success, otherwise returns error code
 *
 * Use open_memstream to write to a message buffer
 */
int SID_cache_store(sid_cursor *cursorp,
                    FILE *outpfile,
                    int db_records,
                    SID_server_list_t *sid_list)
{
    int ret = 0;
    int i = 0;
    int own_txn = 0;
    int own_cursor = 0;
    struct sid_data key;
    struct sid_data val;
/* V3 old version */
#if 0
    char *ATTRS = "ATTRS: ";       /* Tag for the number of attributes on */
                                   /*     the first line of dump file */
    char *SIDS = "SIDS: ";         /* Tag for the number of sids on the */
                                   /*     first line of the dump file */
#endif
    char tmp_sid_str[SID_STR_LEN];
    PVFS_SID tmp_sid;
    SID_cacheval_t *tmp_sid_attrs = NULL;
    PVFS_SID sid_buffer;
    int db_flags;
    struct server_configuration_s *config_s = NULL;

    (void)db_records;
    if (!outpfile || !SID_db)
    {
        return -PVFS_EINVAL;
    }

    /* First Write SID File Header */
    fprintf(outpfile, "<ServerDefines>\n");

/* V3 old version */
#if 0
    /* Write the number of attributes in
     * the cache to the dump file's first line
     */
    fprintf(outpfile, "%s", ATTRS);
    fprintf(outpfile, "%d\t", SID_NUM_ATTR);

    /* Write the number of sids in the cache
     * to the dump file's first line
     */
    fprintf(outpfile, "%s", SIDS);
    fprintf(outpfile, "%d\n", db_records);

    /* Write the string representation of
     * the attributes in the sid cache to
     * the dump file's second line
     */
    for (i = 0; i < SID_NUM_ATTR; i++)
    {
       fprintf(outpfile, "%s ", SID_attr_map[i]);
    }
    fprintf(outpfile, "%c", '\n');
#endif

    config_s = PINT_server_config_mgr_get_config(PVFS_FS_ID_NULL);

    /* One read transaction covers the primary cursor and the type
     * cursor SID_type_store opens. Two cursors cannot each hold the lock.
     * sid_db_txn_begin fails with -PVFS_EINVAL when one is already open.
     */
    ret = sid_db_txn_begin(SID_envp, 1);
    if (ret == 0)
    {
        own_txn = 1;
    }
    else if (ret != -PVFS_EINVAL)
    {
        return ret;
    }
    if (!cursorp)
    {
        ret = sid_db_cursor(SID_db, &cursorp, 1);
        if (ret)
        {
            if (own_txn)
            {
                sid_db_txn_abort(SID_envp);
            }
            return ret;
        }
        own_cursor = 1;
    }

    SID_zero_dbt(&key, &val, NULL);

    /* this routine can either output a whole db or a list of SIDs
     */
    if (sid_list)
    {
        db_flags = SID_DB_CURSOR_SET;
        if (SID_pop_query_list(sid_list, &sid_buffer, NULL, NULL, 0) != 0)
        {
            ret = 0;
            goto done;
        }
        key.data = &sid_buffer;
        key.len = sizeof(PVFS_SID);
    }
    else
    {
        db_flags = SID_DB_CURSOR_FIRST;
    }

    /* Iterate over the database to get the sids */
    while (sid_db_cursor_get(cursorp, &key, &val, db_flags) == 0)
    {
        char *alias = NULL;

        if (val.data)
        {
            SID_cacheval_unpack(&tmp_sid_attrs, &val);
            free(val.data);
            val.data = NULL;
        }
        else
        {
            fprintf(outpfile, "\tERROR val.data empty after get\n");
            ret = -1;
            goto done;
        }
        if (!tmp_sid_attrs)
        {
            if (db_flags != SID_DB_CURSOR_SET)
            {
                free(key.data);
            }
            ret = -PVFS_ENOMEM;
            goto done;
        }

        PVFS_SID_cpy(&tmp_sid, (PVFS_SID *)key.data);
        PVFS_SID_bin2str(&tmp_sid, tmp_sid_str);

        /* this is a sequential search - probably should make hash tbl */
        alias = PINT_config_get_host_alias_ptr(config_s, tmp_sid_attrs->url);

        fprintf(outpfile, "\t<ServerDef>\n");
        if (alias)
        {
            fprintf(outpfile, "\t\tAlias %s\n", alias);
        }
        fprintf(outpfile, "\t\tSID %s\n", tmp_sid_str);
        fprintf(outpfile, "\t\tAddress %s(%lld)\n",
                tmp_sid_attrs->url, lld(tmp_sid_attrs->bmi_addr));

/* V3 old version */
#if 0
        /* Write SID and address info */
        fprintf(outpfile, "%s ", tmp_sid_str);
        fprintf(outpfile, "%lld ", lld(tmp_sid_attrs->bmi_addr));
        fprintf(outpfile, "%s ", tmp_sid_attrs->url);
#endif

        /* Write the user attributes to the dump file */
        fprintf(outpfile, "\t\tAttributes ");
        for (i = 0; i < SID_NUM_ATTR; i++)
        {
            fprintf(outpfile,
                    "%s=%d ",
                    SID_attr_map[i],
                    tmp_sid_attrs->attr[i]);
/* V3 old version */
#if 0
            fprintf(outpfile, "%d ", tmp_sid_attrs->attr[i]);
#endif
        }
        fprintf(outpfile, "\n"); /* end of attributes */

        /* Write system attributes for the server */
        SID_type_store(&tmp_sid, outpfile);

        fprintf(outpfile, "\t</ServerDef>\n");

        SID_cacheval_free(&tmp_sid_attrs);

        if (sid_list)
        {
            if (qlist_empty(&sid_list->link))
            {
                break;
            }
            if (SID_pop_query_list(sid_list, &sid_buffer, NULL, NULL, 0) != 0)
            {
                break;
            }
            key.data = &sid_buffer;
            key.len = sizeof(PVFS_SID);
            db_flags = SID_DB_CURSOR_SET;
        }
        else
        {
            free(key.data);
            key.data = NULL;
            SID_zero_dbt(&key, &val, NULL);
            db_flags = SID_DB_CURSOR_NEXT;
        }
    }

    fprintf(outpfile, "</ServerDefines>\n");
    ret = 0;

done:
    if (own_cursor)
    {
        sid_db_cursor_close(cursorp);
    }
    if (own_txn)
    {
        sid_db_txn_abort(SID_envp);
    }
    return ret;
}

/*
 * This function retrieves entries from a primary database and stores
 * them into a bulk buffer.
 *
 * The minimum amount that can be retrieved is 8 KB of entries.
 *
 * You must specify a size larger than 8 KB by putting an amount into the
 * two size parameters.
 *
 * The output buffer is malloc'ed so take care to make sure it is freed,
 * either by using it in a bulk_insert or by manually freeing.
 *
 * If the cache is larger than the buffer, the entry that does not fit is
 * saved in bulk_next_key.
 *
 * DB_MULTIPLE is a loop of sid_db_cursor_get. Each record is
 * key length, key bytes, value length, value bytes.
 */
int SID_bulk_retrieve_from_sid_cache(int size_of_retrieve_kb,
                                     int size_of_retrieve_mb,
                                     sid_db *dbp,
                                     sid_cursor **dbcursorp,
                                     struct sid_data *output)
{
    int ret = 0;
    int size = 0;
    int used = 0;
    int op;
    char *buf = NULL;
    struct sid_data key;
    struct sid_data val;

    if (!dbp || !dbcursorp || !output)
    {
        return -1;
    }
    /* If the input size of the retrieve is
     * smaller than the minimum size then exit function with error
     */
    if (BULK_MIN_SIZE > (size_of_retrieve_kb * KILOBYTE) +
                        (size_of_retrieve_mb * MEGABYTE))
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                  "Size of bulk retrieve buffer must be greater than 8 KB\n");
        return -1;
    }

    /* If cursor is open, close it so we can reopen it */
    if (*dbcursorp != NULL)
    {
        sid_db_cursor_close(*dbcursorp);
        *dbcursorp = NULL;
    }

    /* Calculate size of buffer as size of kb + size of mb */
    size = (size_of_retrieve_kb * KILOBYTE) + (size_of_retrieve_mb * MEGABYTE);

    output->data = malloc((size_t)size);
    if (output->data == NULL)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG, "Error sizing buffer\n");
        return -1;
    }
    output->len = 0;
    buf = (char *)output->data;

    if ((ret = sid_db_cursor(dbp, dbcursorp, 1)) != 0)
    {
        free(output->data);
        output->data = NULL;
        gossip_debug(GOSSIP_SIDCACHE_DEBUG, "Error creating bulk cursor\n");
        return ret;
    }

    SID_zero_dbt(&key, &val, NULL);
    if (bulk_next_key.data)
    {
        key = bulk_next_key;
        op = SID_DB_CURSOR_SET_RANGE;
    }
    else
    {
        op = SID_DB_CURSOR_FIRST;
    }
    ret = sid_db_cursor_get(*dbcursorp, &key, &val, op);
    while (ret == 0)
    {
        uint32_t klen;
        uint32_t vlen;
        int need;

        klen = (uint32_t)key.len;
        vlen = (uint32_t)val.len;
        need = (int)(sizeof(klen) + klen + sizeof(vlen) + vlen);
        if (used + need > size)
        {
            free(bulk_next_key.data);
            bulk_next_key.data = malloc(key.len ? key.len : 1);
            if (bulk_next_key.data && key.data)
            {
                memcpy(bulk_next_key.data, key.data, key.len);
                bulk_next_key.len = key.len;
            }
            free(key.data);
            free(val.data);
            break;
        }
        memcpy(buf + used, &klen, sizeof(klen));
        used += (int)sizeof(klen);
        if (klen && key.data)
        {
            memcpy(buf + used, key.data, klen);
        }
        used += (int)klen;
        memcpy(buf + used, &vlen, sizeof(vlen));
        used += (int)sizeof(vlen);
        if (vlen && val.data)
        {
            memcpy(buf + used, val.data, vlen);
        }
        used += (int)vlen;
        free(key.data);
        free(val.data);
        SID_zero_dbt(&key, &val, NULL);
        ret = sid_db_cursor_get(*dbcursorp, &key, &val, SID_DB_CURSOR_NEXT);
    }
    sid_db_cursor_close(*dbcursorp);
    *dbcursorp = NULL;
    output->len = (size_t)used;
    if (ret == -PVFS_ENOENT)
    {
        free(bulk_next_key.data);
        bulk_next_key.data = NULL;
        bulk_next_key.len = 0;
        return 0;
    }
    return ret == 0 ? 0 : ret;
}

/*
 * This function inserts entries from the input bulk buffer into a database.
 *
 * The function uses the output from SID_bulk_retrieve as its input.
 *
 * The malloc'ed bulk buffer is freed at the end of this function.
 */
int SID_bulk_insert_into_sid_cache(sid_db *dbp, struct sid_data *input)
{
    int ret = 0;
    char *p;
    char *end;

    if (!dbp || !input || !input->data)
    {
        gossip_err("%s: failed to malloc buffer\n", __func__);
        return -1;
    }
    p = (char *)input->data;
    end = p + input->len;
    while (p + (int)(sizeof(uint32_t) * 2) <= end)
    {
        uint32_t klen = 0;
        uint32_t vlen = 0;
        struct sid_data key;
        struct sid_data val;

        memcpy(&klen, p, sizeof(klen));
        p += sizeof(klen);
        if (p + klen + sizeof(vlen) > end)
        {
            break;
        }
        SID_zero_dbt(&key, &val, NULL);
        key.data = p;
        key.len = klen;
        p += klen;
        memcpy(&vlen, p, sizeof(vlen));
        p += sizeof(vlen);
        if (p + vlen > end)
        {
            break;
        }
        val.data = p;
        val.len = vlen;
        p += vlen;
        ret = sid_db_put(dbp, &key, &val);
        if (ret)
        {
            break;
        }
    }
    free(input->data);
    input->data = NULL;
    input->len = 0;
    return ret;
}


int SID_create_environment(sid_env **envp)
{
    int rc;

    if (!envp)
    {
        return -PVFS_EINVAL;
    }
    /* LMDB needs a directory. This one is not the cache of record.
     * SID_load and SID_save are the text dump. The #if 0 path is
     * <meta_path>/SIDcache.
     */
    snprintf(sid_dir, sizeof(sid_dir), "/tmp/pvfs2-sidcache.%d", (int)getpid());
    sid_reset_dir(sid_dir);
    rc = sid_db_env_open(sid_dir, envp);
    return rc;
}

int SID_create_sid_cache(sid_env *envp, sid_db **dbp)
{
    if (!envp || !dbp)
    {
        return -PVFS_EINVAL;
    }
    SID_zero_dbt(&bulk_next_key, NULL, NULL);
    return sid_db_open(envp, "sid", SID_DB_COMPARE_MEM, 0, dbp);
}

int SID_create_secondary_dbs(sid_env *envp,
                             sid_db *dbp,
                             sid_db *secondary_dbs[],
                             int (*secdbs_callback_functions[])(
                                 sid_db *pri,
                                 const struct sid_data *pkey,
                                 const struct sid_data *pdata,
                                 struct sid_data *skey))
{
    int i;
    int rc;
    char name[16];

    (void)dbp;
    if (!envp || !secondary_dbs || !secdbs_callback_functions)
    {
        return -PVFS_EINVAL;
    }
    /* The callbacks are SID_extract_key, the old associate callbacks.
     * SID_cache_put calls them when it writes the attribute databases.
     * LMDB has no associate, so the secondary records are written there.
     */
    rc = SID_initialize_secondary_dbs(secondary_dbs);
    if (rc)
    {
        return rc;
    }
    for (i = 0; i < SID_NUM_ATTR; i++)
    {
        snprintf(name, sizeof(name), "attr%d", i);
        rc = sid_db_open(envp, name, SID_DB_COMPARE_MEM, SID_DB_DUP,
                         &secondary_dbs[i]);
        if (rc)
        {
            return rc;
        }
    }
    return 0;
}

static int SID_create_type_table(void)
{
    int rc;

    rc = sid_db_open(SID_envp, "type", SID_DB_COMPARE_TYPE, SID_DB_DUP,
                     &SID_type_db);
    if (rc)
    {
        return rc;
    }
    return sid_db_open(SID_envp, "typesid", SID_DB_COMPARE_MEM, SID_DB_DUP,
                       &SID_type_index);
}

int SID_create_dbcs(sid_db *secondary_dbs[], sid_cursor *db_cursors[])
{
    int i;

    (void)secondary_dbs;
    if (!db_cursors)
    {
        return -PVFS_EINVAL;
    }
    /* The Berkeley DB code opened these for the life of the cache.
     * An LMDB cursor holds the environment lock, so they stay NULL.
     * SID_do_join and SID_type_store open a cursor for the call.
     */
    for (i = 0; i < SID_NUM_ATTR; i++)
    {
        db_cursors[i] = NULL;
    }
    SID_type_cursor = NULL;
    SID_index_cursor = NULL;
    return 0;
}

int SID_close_dbcs(sid_cursor *db_cursors[])
{
    int i;

    if (!db_cursors)
    {
        return 0;
    }
    for (i = 0; i < SID_NUM_ATTR; i++)
    {
        if (db_cursors[i])
        {
            sid_db_cursor_close(db_cursors[i]);
            db_cursors[i] = NULL;
        }
    }
    if (SID_type_cursor)
    {
        sid_db_cursor_close(SID_type_cursor);
        SID_type_cursor = NULL;
    }
    if (SID_index_cursor)
    {
        sid_db_cursor_close(SID_index_cursor);
        SID_index_cursor = NULL;
    }
    return 0;
}

int SID_close_dbs_env(sid_env *envp, sid_db *dbp, sid_db *secondary_dbs[])
{
    int i;

    (void)dbp;
    if (envp)
    {
        sid_db_env_close(envp);
    }
    free(SID_db);
    SID_db = NULL;
    free(SID_type_db);
    SID_type_db = NULL;
    free(SID_type_index);
    SID_type_index = NULL;
    if (secondary_dbs)
    {
        for (i = 0; i < SID_NUM_ATTR; i++)
        {
            free(secondary_dbs[i]);
            secondary_dbs[i] = NULL;
        }
    }
    if (envp == SID_envp)
    {
        SID_envp = NULL;
    }
    sid_reset_dir(sid_dir);
    rmdir(sid_dir);
    return 0;
}

int SID_initialize(void)
{
    int ret;

    if (SID_envp)
    {
        return 0;
    }
    memset(SID_attr_index, 0, sizeof(SID_attr_index));
    memset(SID_attr_cursor, 0, sizeof(SID_attr_cursor));
    ret = SID_create_environment(&SID_envp);
    if (ret)
    {
        return ret;
    }
    ret = SID_create_sid_cache(SID_envp, &SID_db);
    if (ret)
    {
        goto errorout;
    }
    ret = SID_create_secondary_dbs(SID_envp, SID_db, SID_attr_index,
                                   SID_extract_key);
    if (ret)
    {
        goto errorout;
    }
    ret = SID_create_type_table();
    if (ret)
    {
        goto errorout;
    }
    ret = SID_create_dbcs(SID_attr_index, SID_attr_cursor);
    if (ret)
    {
        goto errorout;
    }
    sids_in_cache = 0;
    return 0;

errorout:
    SID_close_dbcs(SID_attr_cursor);
    SID_close_dbs_env(SID_envp, SID_db, SID_attr_index);
    return ret;
}

int SID_load(const char *path)
{
    FILE *inpfile;
    int ret;
    char *filename = NULL;
    struct stat sbuf;
    PVFS_fs_id fsid __attribute__ ((unused)) = PVFS_FS_ID_NULL;

    if (!path)
    {
#if 0
        /* figure out the path to the cached data file */
        struct server_configuration_s *srv_conf;
        int fnlen;
        srv_conf = PINT_server_config_mgr_get_config(fsid);
        fnlen = strlen(srv_conf->meta_path) + strlen("/SIDcache");
        filename = (char *)malloc(fnlen + 1);
        strncpy(filename, srv_conf->meta_path, fnlen + 1);
        strncat(filename, "/SIDcache", fnlen + 1);
        PINT_server_config_mgr_put_config(srv_conf);
#endif
        /* LMDB still needs a directory. The text file is the cache
         * of record. There is no default path until the block above
         * is finished.
         */
        return 0;
    }
    filename = (char *)path;
    ret = stat(filename, &sbuf);
    if (ret < 0)
    {
        if (errno == ENOENT)
        {
            errno = 0;
            return 0;
        }
        return -PVFS_EIO;
    }
    inpfile = fopen(filename, "r");
    if (!inpfile)
    {
        gossip_err("Could not open the file %s in "
                   "SID_load_cache_from_file\n",
                   filename);
        return -PVFS_EIO;
    }
    ret = SID_cache_load(SID_db, inpfile, &sids_in_cache);
    fclose(inpfile);
    return ret;
}

int SID_loadbuffer(const char *buffer, int size)
{
    FILE *inpfile;
    int ret;

    if (!buffer || size < 0)
    {
        return -PVFS_EINVAL;
    }
    inpfile = fmemopen((void *)buffer, (size_t)size, "r");
    if (!inpfile)
    {
        return -PVFS_EIO;
    }
    ret = SID_cache_load(SID_db, inpfile, &sids_in_cache);
    fclose(inpfile);
    return ret;
}

int SID_save(const char *path)
{
    FILE *outpfile = NULL;
    int ret;
    int close_file = 1;
    char *filename = NULL;
    PVFS_fs_id fsid __attribute__ ((unused)) = PVFS_FS_ID_NULL;

    if (!path || !path[0])
    {
#if 0
        /* figure out the path to the cached data file */
        struct server_configuration_s *srv_conf;
        int fnlen;
        srv_conf = PINT_server_config_mgr_get_config(fsid);
        fnlen = strlen(srv_conf->meta_path) + strlen("/SIDcache");
        filename = (char *)malloc(fnlen + 1);
        strncpy(filename, srv_conf->meta_path, fnlen + 1);
        strncat(filename, "/SIDcache", fnlen + 1);
        PINT_server_config_mgr_put_config(srv_conf);
#endif
        outpfile = stderr; /* debugging goes to stderr */
        close_file = 0;
        (void)filename;
    }
    else
    {
        filename = (char *)path;
        outpfile = fopen(filename, "w");
    }
    if (!outpfile)
    {
        gossip_err("Error opening dump file in SID_save function\n");
        return -PVFS_EIO;
    }
    ret = SID_cache_store(NULL, outpfile, sids_in_cache, NULL);
    if (close_file)
    {
        fclose(outpfile);
    }
    return ret;
}

int SID_savelist(char *buffer, int size, SID_server_list_t *slist)
{
    FILE *outpfile;
    int ret;

    if (!buffer || size <= 0 || !slist)
    {
        return -PVFS_EINVAL;
    }
    outpfile = fmemopen(buffer, (size_t)size, "w");
    if (!outpfile)
    {
        return -PVFS_EIO;
    }
    /* dump cache records for the SIDs in the list */
    ret = SID_cache_store(NULL, outpfile, size, slist);
    fclose(outpfile);
    return ret;
}

int SID_add(const PVFS_SID *sid,
            PVFS_BMI_addr_t bmi_addr,
            const char *url,
            int attributes[])
{
    SID_cacheval_t *cval = NULL;
    int ret;

    ret = SID_cacheval_alloc(&cval, attributes, (BMI_addr)bmi_addr, url);
    if (ret)
    {
        return ret;
    }
    if (cval->bmi_addr == 0)
    {
        ret = BMI_addr_lookup(&cval->bmi_addr, cval->url, NULL);
        if (ret != 0 && ret != -BMI_NOTINITIALIZED)
        {
            SID_cacheval_free(&cval);
            return ret;
        }
    }
    ret = SID_cache_put(SID_db, sid, cval, &sids_in_cache);
    SID_cacheval_free(&cval);
    return ret;
}

int SID_delete(const PVFS_SID *sid)
{
    return SID_cache_delete_server(SID_db, sid, &sids_in_cache);
}

int SID_finalize(void)
{
    int ret;

    if (!SID_envp)
    {
        return 0;
    }
#if 0
    /* save cache contents to a file */
    ret = SID_save(NULL);
    if (ret < 0)
    {
        return ret;
    }
#endif
    ret = SID_close_dbcs(SID_attr_cursor);
    if (ret)
    {
        return ret;
    }
    ret = SID_close_dbs_env(SID_envp, SID_db, SID_attr_index);
    return ret;
}

int SID_update_type(const PVFS_SID *sid, struct SID_type_s *new_server_type)
{
    return SID_cache_update_type(sid, new_server_type);
}

int SID_update_type_single(const PVFS_SID *sid,
                           struct SID_type_s *new_server_type)
{
    if (!new_server_type)
    {
        return -PVFS_EINVAL;
    }
    return SID_cache_update_type_single(sid,
                                        new_server_type->server_type,
                                        new_server_type->fsid);
}

int SID_update_attributes(const PVFS_SID *sid_server, int new_attr[])
{
    return SID_cache_update_attrs(SID_db, sid_server, new_attr);
}
