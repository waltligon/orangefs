/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 *
 * SID cache storage is LMDB. The cache is private to this process:
 * SID_initialize recreates /tmp/pvfs2-sidcache.<pid> and SID_finalize
 * removes it. Trove has its own LMDB environments.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <lmdb.h>

#include "pvfs2-internal.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "pvfs2-types.h"
#include "pvfs3-handle.h"
#include "gen-locks.h"
#include "bmi.h"
#include "sidcache.h"

#define SID_MAPSIZE ((size_t)32 * 1024 * 1024)
#define SID_MAXDBS 16

static gen_mutex_t sid_mu;
static int sid_ready = 0;
static MDB_env *sid_env = NULL;
static MDB_dbi sid_dbi = 0;
static MDB_dbi attr_dbi[SID_NUM_ATTR];
static MDB_dbi type_dbi = 0;
static MDB_dbi typesid_dbi = 0;
static char sid_dir[64];
static int sids_in_cache = 0;

static int scan_valid = 0;
static struct SID_type_s scan_key;
static PVFS_SID scan_sid;

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

static void sid_lock(void)
{
    gen_mutex_lock(&sid_mu);
}

static void sid_unlock(void)
{
    gen_mutex_unlock(&sid_mu);
}

static int type_cmp(const MDB_val *a, const MDB_val *b)
{
    const struct SID_type_s *ta;
    const struct SID_type_s *tb;

    if (a->mv_size < sizeof(struct SID_type_s) ||
        b->mv_size < sizeof(struct SID_type_s))
    {
        return (a->mv_size < b->mv_size) ? -1 : 1;
    }
    ta = (const struct SID_type_s *)a->mv_data;
    tb = (const struct SID_type_s *)b->mv_data;
    if (ta->fsid < tb->fsid)
    {
        return -1;
    }
    if (ta->fsid > tb->fsid)
    {
        return 1;
    }
    if (ta->server_type < tb->server_type)
    {
        return -1;
    }
    if (ta->server_type > tb->server_type)
    {
        return 1;
    }
    return 0;
}

static int type_equal(const struct SID_type_s *a, const struct SID_type_s *b)
{
    return a->fsid == b->fsid && a->server_type == b->server_type;
}

static void sid_reset_dir(void)
{
    char path[80];

    snprintf(path, sizeof(path), "%s/data.mdb", sid_dir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/lock.mdb", sid_dir);
    unlink(path);
    rmdir(sid_dir);
    mkdir(sid_dir, 0700);
}

static void sid_remove_dir(void)
{
    char path[80];

    if (sid_dir[0] == '\0')
    {
        return;
    }
    snprintf(path, sizeof(path), "%s/data.mdb", sid_dir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/lock.mdb", sid_dir);
    unlink(path);
    rmdir(sid_dir);
}

static size_t sid_pack_size(const SID_cacheval_t *cacheval)
{
    return sizeof(SID_cacheval_t) + strlen(cacheval->url) + 1;
}

static SID_cacheval_t *sid_unpack(const MDB_val *val)
{
    SID_cacheval_t *cacheval;

    if (val->mv_size < sizeof(SID_cacheval_t))
    {
        return NULL;
    }
    cacheval = (SID_cacheval_t *)malloc(val->mv_size);
    if (!cacheval)
    {
        return NULL;
    }
    memcpy(cacheval, val->mv_data, val->mv_size);
    cacheval->url = (char *)&cacheval[1];
    return cacheval;
}

static int sid_open_dbis(MDB_txn *txn)
{
    int rc;
    int i;
    char name[16];

    rc = mdb_dbi_open(txn, "sid", MDB_CREATE, &sid_dbi);
    if (rc)
    {
        return rc;
    }
    for (i = 0; i < SID_NUM_ATTR; i++)
    {
        snprintf(name, sizeof(name), "attr%d", i);
        rc = mdb_dbi_open(txn, name, MDB_CREATE | MDB_DUPSORT, &attr_dbi[i]);
        if (rc)
        {
            return rc;
        }
    }
    rc = mdb_dbi_open(txn, "type", MDB_CREATE | MDB_DUPSORT, &type_dbi);
    if (rc)
    {
        return rc;
    }
    mdb_set_compare(txn, type_dbi, type_cmp);
    rc = mdb_dbi_open(txn, "typesid", MDB_CREATE | MDB_DUPSORT, &typesid_dbi);
    return rc;
}

static int sid_get_u(const PVFS_SID *sid, SID_cacheval_t **cacheval)
{
    MDB_txn *txn = NULL;
    MDB_val key;
    MDB_val val;
    int rc;

    *cacheval = NULL;
    if (!sid_env)
    {
        return -PVFS_EINVAL;
    }
    rc = mdb_txn_begin(sid_env, NULL, MDB_RDONLY, &txn);
    if (rc)
    {
        return -PVFS_EIO;
    }
    key.mv_data = (void *)sid;
    key.mv_size = sizeof(*sid);
    rc = mdb_get(txn, sid_dbi, &key, &val);
    if (rc == MDB_NOTFOUND)
    {
        mdb_txn_abort(txn);
        return -PVFS_ENOENT;
    }
    if (rc)
    {
        mdb_txn_abort(txn);
        return -PVFS_EIO;
    }
    *cacheval = sid_unpack(&val);
    mdb_txn_abort(txn);
    if (!*cacheval)
    {
        return -PVFS_ENOMEM;
    }
    return 0;
}

static int sid_del_attr_u(MDB_txn *txn, int32_t attr, const PVFS_SID *sid, int ix)
{
    MDB_val key;
    MDB_val val;

    key.mv_data = &attr;
    key.mv_size = sizeof(attr);
    val.mv_data = (void *)sid;
    val.mv_size = sizeof(*sid);
    return mdb_del(txn, attr_dbi[ix], &key, &val);
}

static int sid_put_attr_u(MDB_txn *txn, int32_t attr, const PVFS_SID *sid, int ix)
{
    MDB_val key;
    MDB_val val;

    key.mv_data = &attr;
    key.mv_size = sizeof(attr);
    val.mv_data = (void *)sid;
    val.mv_size = sizeof(*sid);
    return mdb_put(txn, attr_dbi[ix], &key, &val, MDB_NODUPDATA);
}

static int sid_index_attrs_u(MDB_txn *txn,
                             const PVFS_SID *sid,
                             const SID_cacheval_t *fresh,
                             const SID_cacheval_t *old)
{
    int i;
    int rc;

    for (i = 0; i < SID_NUM_ATTR; i++)
    {
        int32_t new_key = fresh->attr[i];
        if (old && old->attr[i] == new_key)
        {
            continue;
        }
        if (old)
        {
            rc = sid_del_attr_u(txn, old->attr[i], sid, i);
            if (rc && rc != MDB_NOTFOUND)
            {
                return rc;
            }
        }
        rc = sid_put_attr_u(txn, new_key, sid, i);
        if (rc && rc != MDB_KEYEXIST)
        {
            return rc;
        }
    }
    return 0;
}

static int sid_put_u(const PVFS_SID *sid,
                     const SID_cacheval_t *cacheval,
                     int insert_only)
{
    MDB_txn *txn = NULL;
    MDB_val key;
    MDB_val val;
    MDB_val oldval;
    SID_cacheval_t *old = NULL;
    int rc;
    int existed = 0;

    if (!sid_env || !cacheval || !cacheval->url || PVFS_SID_is_null(sid))
    {
        return -PVFS_EINVAL;
    }
    rc = mdb_txn_begin(sid_env, NULL, 0, &txn);
    if (rc)
    {
        return -PVFS_EIO;
    }
    key.mv_data = (void *)sid;
    key.mv_size = sizeof(*sid);
    rc = mdb_get(txn, sid_dbi, &key, &oldval);
    if (rc == 0)
    {
        existed = 1;
        if (insert_only)
        {
            mdb_txn_abort(txn);
            return 0;
        }
        old = sid_unpack(&oldval);
        if (!old)
        {
            mdb_txn_abort(txn);
            return -PVFS_ENOMEM;
        }
    }
    else if (rc != MDB_NOTFOUND)
    {
        mdb_txn_abort(txn);
        return -PVFS_EIO;
    }

    val.mv_data = (void *)cacheval;
    val.mv_size = sid_pack_size(cacheval);
    rc = mdb_put(txn, sid_dbi, &key, &val, 0);
    if (rc)
    {
        free(old);
        mdb_txn_abort(txn);
        return -PVFS_EIO;
    }
    rc = sid_index_attrs_u(txn, sid, cacheval, old);
    free(old);
    if (rc)
    {
        mdb_txn_abort(txn);
        return -PVFS_EIO;
    }
    rc = mdb_txn_commit(txn);
    if (rc)
    {
        return -PVFS_EIO;
    }
    if (!existed)
    {
        sids_in_cache++;
    }
    return 0;
}

static int sid_del_types_u(MDB_txn *txn, const PVFS_SID *sid)
{
    MDB_cursor *cur = NULL;
    MDB_val key;
    MDB_val val;
    int rc;

    rc = mdb_cursor_open(txn, typesid_dbi, &cur);
    if (rc)
    {
        return rc;
    }
    key.mv_data = (void *)sid;
    key.mv_size = sizeof(*sid);
    rc = mdb_cursor_get(cur, &key, &val, MDB_SET);
    while (rc == 0)
    {
        struct SID_type_s type;
        MDB_val tkey;
        MDB_val tval;

        if (val.mv_size == sizeof(type))
        {
            memcpy(&type, val.mv_data, sizeof(type));
            tkey.mv_data = &type;
            tkey.mv_size = sizeof(type);
            tval.mv_data = (void *)sid;
            tval.mv_size = sizeof(*sid);
            rc = mdb_del(txn, type_dbi, &tkey, &tval);
            if (rc && rc != MDB_NOTFOUND)
            {
                mdb_cursor_close(cur);
                return rc;
            }
        }
        rc = mdb_cursor_del(cur, 0);
        if (rc)
        {
            mdb_cursor_close(cur);
            return rc;
        }
        rc = mdb_cursor_get(cur, &key, &val, MDB_NEXT_DUP);
    }
    mdb_cursor_close(cur);
    if (rc == MDB_NOTFOUND)
    {
        return 0;
    }
    return rc;
}

static int sid_delete_u(const PVFS_SID *sid)
{
    MDB_txn *txn = NULL;
    MDB_val key;
    MDB_val val;
    SID_cacheval_t *old = NULL;
    int i;
    int rc;

    if (!sid_env)
    {
        return -PVFS_EINVAL;
    }
    rc = mdb_txn_begin(sid_env, NULL, 0, &txn);
    if (rc)
    {
        return -PVFS_EIO;
    }
    key.mv_data = (void *)sid;
    key.mv_size = sizeof(*sid);
    rc = mdb_get(txn, sid_dbi, &key, &val);
    if (rc == MDB_NOTFOUND)
    {
        mdb_txn_abort(txn);
        return -PVFS_ENOENT;
    }
    if (rc)
    {
        mdb_txn_abort(txn);
        return -PVFS_EIO;
    }
    old = sid_unpack(&val);
    if (!old)
    {
        mdb_txn_abort(txn);
        return -PVFS_ENOMEM;
    }
    for (i = 0; i < SID_NUM_ATTR; i++)
    {
        rc = sid_del_attr_u(txn, old->attr[i], sid, i);
        if (rc && rc != MDB_NOTFOUND)
        {
            free(old);
            mdb_txn_abort(txn);
            return -PVFS_EIO;
        }
    }
    free(old);
    rc = sid_del_types_u(txn, sid);
    if (rc)
    {
        mdb_txn_abort(txn);
        return -PVFS_EIO;
    }
    rc = mdb_del(txn, sid_dbi, &key, NULL);
    if (rc)
    {
        mdb_txn_abort(txn);
        return -PVFS_EIO;
    }
    rc = mdb_txn_commit(txn);
    if (rc)
    {
        return -PVFS_EIO;
    }
    if (sids_in_cache > 0)
    {
        sids_in_cache--;
    }
    scan_valid = 0;
    return 0;
}

/* from_key selects the SID out of the key. Attribute and type
 * duplicates store the SID in the value. The primary stores it in the key.
 */
static int sid_collect_dups(MDB_dbi dbi, MDB_val *key, int from_key,
                            PVFS_SID **sids, int *n)
{
    MDB_txn *txn = NULL;
    MDB_cursor *cur = NULL;
    MDB_val val;
    MDB_val local_key;
    MDB_val *k;
    int rc;
    int count = 0;
    PVFS_SID *buf = NULL;

    *sids = NULL;
    *n = 0;
    memset(&local_key, 0, sizeof(local_key));
    k = key ? key : &local_key;
    rc = mdb_txn_begin(sid_env, NULL, MDB_RDONLY, &txn);
    if (rc)
    {
        return -PVFS_EIO;
    }
    rc = mdb_cursor_open(txn, dbi, &cur);
    if (rc)
    {
        mdb_txn_abort(txn);
        return -PVFS_EIO;
    }
    rc = mdb_cursor_get(cur, k, &val, key ? MDB_SET : MDB_FIRST);
    while (rc == 0)
    {
        PVFS_SID *grown;
        MDB_val *src = from_key ? k : &val;
        if (src->mv_size != sizeof(PVFS_SID))
        {
            rc = mdb_cursor_get(cur, k, &val, key ? MDB_NEXT_DUP : MDB_NEXT);
            continue;
        }
        grown = (PVFS_SID *)realloc(buf, (count + 1) * sizeof(PVFS_SID));
        if (!grown)
        {
            free(buf);
            mdb_cursor_close(cur);
            mdb_txn_abort(txn);
            return -PVFS_ENOMEM;
        }
        buf = grown;
        memcpy(&buf[count], src->mv_data, sizeof(PVFS_SID));
        count++;
        rc = mdb_cursor_get(cur, k, &val, key ? MDB_NEXT_DUP : MDB_NEXT);
    }
    mdb_cursor_close(cur);
    mdb_txn_abort(txn);
    if (rc != MDB_NOTFOUND && rc != 0)
    {
        free(buf);
        return -PVFS_EIO;
    }
    *sids = buf;
    *n = count;
    return 0;
}

static int sid_type_put_u(const PVFS_SID *sid, struct SID_type_s type)
{
    MDB_txn *txn = NULL;
    MDB_val key;
    MDB_val val;
    int rc;

    rc = mdb_txn_begin(sid_env, NULL, 0, &txn);
    if (rc)
    {
        return -PVFS_EIO;
    }
    key.mv_data = &type;
    key.mv_size = sizeof(type);
    val.mv_data = (void *)sid;
    val.mv_size = sizeof(*sid);
    rc = mdb_put(txn, type_dbi, &key, &val, MDB_NODUPDATA);
    if (rc && rc != MDB_KEYEXIST)
    {
        mdb_txn_abort(txn);
        return -PVFS_EIO;
    }
    key.mv_data = (void *)sid;
    key.mv_size = sizeof(*sid);
    val.mv_data = &type;
    val.mv_size = sizeof(type);
    rc = mdb_put(txn, typesid_dbi, &key, &val, MDB_NODUPDATA);
    if (rc && rc != MDB_KEYEXIST)
    {
        mdb_txn_abort(txn);
        return -PVFS_EIO;
    }
    rc = mdb_txn_commit(txn);
    return rc ? -PVFS_EIO : 0;
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

int SID_initialize(void)
{
    MDB_txn *txn = NULL;
    int rc;

    if (sid_ready)
    {
        return 0;
    }
    gen_mutex_init(&sid_mu);
    snprintf(sid_dir, sizeof(sid_dir), "/tmp/pvfs2-sidcache.%d", (int)getpid());
    sid_reset_dir();
    rc = mdb_env_create(&sid_env);
    if (rc)
    {
        return -PVFS_EIO;
    }
    mdb_env_set_maxdbs(sid_env, SID_MAXDBS);
    rc = mdb_env_set_mapsize(sid_env, SID_MAPSIZE);
    if (rc)
    {
        mdb_env_close(sid_env);
        sid_env = NULL;
        return -PVFS_EIO;
    }
    rc = mdb_env_open(sid_env, sid_dir, MDB_NOSYNC, 0700);
    if (rc)
    {
        mdb_env_close(sid_env);
        sid_env = NULL;
        return -PVFS_EIO;
    }
    rc = mdb_txn_begin(sid_env, NULL, 0, &txn);
    if (rc)
    {
        mdb_env_close(sid_env);
        sid_env = NULL;
        return -PVFS_EIO;
    }
    rc = sid_open_dbis(txn);
    if (rc)
    {
        mdb_txn_abort(txn);
        mdb_env_close(sid_env);
        sid_env = NULL;
        return -PVFS_EIO;
    }
    rc = mdb_txn_commit(txn);
    if (rc)
    {
        mdb_env_close(sid_env);
        sid_env = NULL;
        return -PVFS_EIO;
    }
    sids_in_cache = 0;
    scan_valid = 0;
    sid_ready = 1;
    return 0;
}

int SID_finalize(void)
{
    if (!sid_ready)
    {
        return 0;
    }
    sid_lock();
    if (sid_env)
    {
        mdb_env_close(sid_env);
        sid_env = NULL;
    }
    sid_ready = 0;
    scan_valid = 0;
    sid_unlock();
    sid_remove_dir();
    gen_mutex_destroy(&sid_mu);
    return 0;
}

int SID_cache_get(const PVFS_SID *sid, SID_cacheval_t **cacheval)
{
    int rc;

    sid_lock();
    rc = sid_get_u(sid, cacheval);
    sid_unlock();
    return rc;
}

int SID_cache_put(const PVFS_SID *sid,
                  const SID_cacheval_t *cacheval,
                  int *num_records)
{
    int rc;

    sid_lock();
    rc = sid_put_u(sid, cacheval, num_records != NULL);
    if (rc == 0 && num_records)
    {
        *num_records = sids_in_cache;
    }
    sid_unlock();
    return rc;
}

int SID_add(const PVFS_SID *sid,
            PVFS_BMI_addr_t bmi_addr,
            const char *url,
            int attributes[])
{
    SID_cacheval_t *cval = NULL;
    int rc;

    rc = SID_cacheval_alloc(&cval, attributes, (BMI_addr)bmi_addr, url);
    if (rc)
    {
        return rc;
    }
    if (cval->bmi_addr == 0)
    {
        rc = BMI_addr_lookup(&cval->bmi_addr, cval->url, NULL);
        if (rc != 0 && rc != -BMI_NOTINITIALIZED)
        {
            SID_cacheval_free(&cval);
            return rc;
        }
    }
    rc = SID_cache_put(sid, cval, &sids_in_cache);
    SID_cacheval_free(&cval);
    return rc;
}

int SID_delete(const PVFS_SID *sid)
{
    int rc;

    sid_lock();
    rc = sid_delete_u(sid);
    sid_unlock();
    return rc;
}

int SID_update_type_single(const PVFS_SID *sid,
                           struct SID_type_s *new_server_type)
{
    int rc;

    if (!sid || !new_server_type)
    {
        return -PVFS_EINVAL;
    }
    if ((new_server_type->server_type & SID_SERVER_VALID_TYPES) == 0)
    {
        return 0;
    }
    sid_lock();
    rc = sid_type_put_u(sid, *new_server_type);
    sid_unlock();
    return rc;
}

int SID_update_type(const PVFS_SID *sid, struct SID_type_s *new_type_val)
{
    uint32_t mask;
    uint32_t type_val;
    int rc = 0;

    if (!sid || !new_type_val)
    {
        return -PVFS_EINVAL;
    }
    type_val = new_type_val->server_type;
    for (mask = 1; mask != 0 && type_val != 0; mask <<= 1)
    {
        if (type_val & mask)
        {
            struct SID_type_s one = *new_type_val;
            one.server_type = mask;
            if (mask & SID_SERVER_VALID_TYPES)
            {
                rc = SID_update_type_single(sid, &one);
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

int SID_update_attributes(const PVFS_SID *sid_server, int new_attr[])
{
    SID_cacheval_t *cur = NULL;
    int rc;

    rc = SID_cache_get(sid_server, &cur);
    if (rc)
    {
        return rc;
    }
    memcpy(cur->attr, new_attr, sizeof(int) * SID_NUM_ATTR);
    rc = SID_cache_put(sid_server, cur, NULL);
    SID_cacheval_free(&cur);
    return rc;
}

int SID_get_type(PVFS_SID *sid, uint32_t *typeval)
{
    MDB_txn *txn = NULL;
    MDB_cursor *cur = NULL;
    MDB_val key;
    MDB_val val;
    int rc;

    if (!sid || !typeval)
    {
        return -PVFS_EINVAL;
    }
    *typeval = 0;
    sid_lock();
    if (!sid_env)
    {
        sid_unlock();
        return -PVFS_EINVAL;
    }
    rc = mdb_txn_begin(sid_env, NULL, MDB_RDONLY, &txn);
    if (rc)
    {
        sid_unlock();
        return -PVFS_EIO;
    }
    rc = mdb_cursor_open(txn, typesid_dbi, &cur);
    if (rc)
    {
        mdb_txn_abort(txn);
        sid_unlock();
        return -PVFS_EIO;
    }
    key.mv_data = sid;
    key.mv_size = sizeof(*sid);
    rc = mdb_cursor_get(cur, &key, &val, MDB_SET);
    while (rc == 0)
    {
        struct SID_type_s type;
        if (val.mv_size == sizeof(type))
        {
            memcpy(&type, val.mv_data, sizeof(type));
            *typeval |= type.server_type;
        }
        rc = mdb_cursor_get(cur, &key, &val, MDB_NEXT_DUP);
    }
    mdb_cursor_close(cur);
    mdb_txn_abort(txn);
    sid_unlock();
    if (rc != MDB_NOTFOUND && rc != 0)
    {
        return -PVFS_EIO;
    }
    return 0;
}

int SID_attr_list(int attr, int32_t value, PVFS_SID **sids, int *n)
{
    MDB_val key;
    int rc;

    if (attr < 0 || attr >= SID_NUM_ATTR || !sids || !n)
    {
        return -PVFS_EINVAL;
    }
    key.mv_data = &value;
    key.mv_size = sizeof(value);
    sid_lock();
    if (!sid_env)
    {
        sid_unlock();
        return -PVFS_EINVAL;
    }
    rc = sid_collect_dups(attr_dbi[attr], &key, 0, sids, n);
    sid_unlock();
    return rc;
}

int SID_list_all(PVFS_SID **sids, int *n)
{
    int rc;

    if (!sids || !n)
    {
        return -PVFS_EINVAL;
    }
    sid_lock();
    if (!sid_env)
    {
        sid_unlock();
        return -PVFS_EINVAL;
    }
    rc = sid_collect_dups(sid_dbi, NULL, 1, sids, n);
    sid_unlock();
    return rc;
}

int SID_type_count(const struct SID_type_s *key, int *count)
{
    MDB_txn *txn = NULL;
    MDB_cursor *cur = NULL;
    MDB_val k;
    MDB_val v;
    int rc;
    size_t n = 0;

    if (!key || !count)
    {
        return -PVFS_EINVAL;
    }
    *count = 0;
    sid_lock();
    if (!sid_env)
    {
        sid_unlock();
        return -PVFS_EINVAL;
    }
    rc = mdb_txn_begin(sid_env, NULL, MDB_RDONLY, &txn);
    if (rc)
    {
        sid_unlock();
        return -PVFS_EIO;
    }
    rc = mdb_cursor_open(txn, type_dbi, &cur);
    if (rc)
    {
        mdb_txn_abort(txn);
        sid_unlock();
        return -PVFS_EIO;
    }
    if (key->server_type == SID_SERVER_ALL)
    {
        PVFS_SID *seen = NULL;
        int seen_n = 0;
        rc = mdb_cursor_get(cur, &k, &v, MDB_FIRST);
        while (rc == 0)
        {
            struct SID_type_s type;
            if (k.mv_size == sizeof(type) && v.mv_size == sizeof(PVFS_SID))
            {
                memcpy(&type, k.mv_data, sizeof(type));
                if (type.fsid == key->fsid &&
                    !sid_seen((PVFS_SID *)v.mv_data, seen, seen_n))
                {
                    PVFS_SID *grown = (PVFS_SID *)realloc(
                        seen, (seen_n + 1) * sizeof(PVFS_SID));
                    if (!grown)
                    {
                        free(seen);
                        mdb_cursor_close(cur);
                        mdb_txn_abort(txn);
                        sid_unlock();
                        return -PVFS_ENOMEM;
                    }
                    seen = grown;
                    memcpy(&seen[seen_n], v.mv_data, sizeof(PVFS_SID));
                    seen_n++;
                }
            }
            rc = mdb_cursor_get(cur, &k, &v, MDB_NEXT);
        }
        free(seen);
        *count = seen_n;
    }
    else
    {
        k.mv_data = (void *)key;
        k.mv_size = sizeof(*key);
        rc = mdb_cursor_get(cur, &k, &v, MDB_SET);
        if (rc == 0)
        {
            mdb_cursor_count(cur, &n);
            *count = (int)n;
        }
        else if (rc == MDB_NOTFOUND)
        {
            rc = 0;
        }
    }
    mdb_cursor_close(cur);
    mdb_txn_abort(txn);
    sid_unlock();
    if (rc != 0 && rc != MDB_NOTFOUND)
    {
        return -PVFS_EIO;
    }
    return 0;
}

int SID_type_step(const struct SID_type_s *key, int first, PVFS_SID *sid)
{
    MDB_txn *txn = NULL;
    MDB_cursor *cur = NULL;
    MDB_val k;
    MDB_val v;
    int rc;

    if (!key || !sid)
    {
        return -PVFS_EINVAL;
    }
    sid_lock();
    if (!sid_env)
    {
        sid_unlock();
        return -PVFS_EINVAL;
    }
    rc = mdb_txn_begin(sid_env, NULL, MDB_RDONLY, &txn);
    if (rc)
    {
        sid_unlock();
        return -PVFS_EIO;
    }
    rc = mdb_cursor_open(txn, type_dbi, &cur);
    if (rc)
    {
        mdb_txn_abort(txn);
        sid_unlock();
        return -PVFS_EIO;
    }
    k.mv_data = (void *)key;
    k.mv_size = sizeof(*key);
    if (first || !scan_valid || !type_equal(&scan_key, key))
    {
        rc = mdb_cursor_get(cur, &k, &v, MDB_SET);
    }
    else
    {
        v.mv_data = &scan_sid;
        v.mv_size = sizeof(scan_sid);
        rc = mdb_cursor_get(cur, &k, &v, MDB_GET_BOTH);
        if (rc == 0)
        {
            rc = mdb_cursor_get(cur, &k, &v, MDB_NEXT_DUP);
        }
    }
    if (rc == 0 && v.mv_size == sizeof(PVFS_SID))
    {
        memcpy(sid, v.mv_data, sizeof(PVFS_SID));
        scan_key = *key;
        scan_sid = *sid;
        scan_valid = 1;
        mdb_cursor_close(cur);
        mdb_txn_abort(txn);
        sid_unlock();
        return 0;
    }
    mdb_cursor_close(cur);
    mdb_txn_abort(txn);
    sid_unlock();
    if (rc == MDB_NOTFOUND || rc == 0)
    {
        return -PVFS_ENOENT;
    }
    return -PVFS_EIO;
}

/* Text form used by SID_load and SID_save. One record per line:
 * S <uuid> <bmi> <url> <attr0> .. <attr7>
 * T <uuid> <fsid> <server_type>
 */
static int sid_write_all(FILE *out)
{
    MDB_txn *txn = NULL;
    MDB_cursor *cur = NULL;
    MDB_val key;
    MDB_val val;
    int rc;

    if (!sid_env)
    {
        return -PVFS_EINVAL;
    }
    rc = mdb_txn_begin(sid_env, NULL, MDB_RDONLY, &txn);
    if (rc)
    {
        return -PVFS_EIO;
    }
    fprintf(out, "SIDCACHE 1\n");
    rc = mdb_cursor_open(txn, sid_dbi, &cur);
    if (rc)
    {
        mdb_txn_abort(txn);
        return -PVFS_EIO;
    }
    rc = mdb_cursor_get(cur, &key, &val, MDB_FIRST);
    while (rc == 0)
    {
        SID_cacheval_t *cval;
        int i;
        if (key.mv_size == sizeof(PVFS_SID) &&
            (cval = sid_unpack(&val)) != NULL)
        {
            fprintf(out, "S %s %lld %s",
                    PVFS_SID_str((PVFS_SID *)key.mv_data),
                    (long long)cval->bmi_addr,
                    cval->url);
            for (i = 0; i < SID_NUM_ATTR; i++)
            {
                fprintf(out, " %d", cval->attr[i]);
            }
            fprintf(out, "\n");
            free(cval);
        }
        rc = mdb_cursor_get(cur, &key, &val, MDB_NEXT);
    }
    mdb_cursor_close(cur);
    rc = mdb_cursor_open(txn, type_dbi, &cur);
    if (rc)
    {
        mdb_txn_abort(txn);
        return -PVFS_EIO;
    }
    rc = mdb_cursor_get(cur, &key, &val, MDB_FIRST);
    while (rc == 0)
    {
        struct SID_type_s type;
        if (key.mv_size == sizeof(type) && val.mv_size == sizeof(PVFS_SID))
        {
            memcpy(&type, key.mv_data, sizeof(type));
            fprintf(out, "T %s %d %u\n",
                    PVFS_SID_str((PVFS_SID *)val.mv_data),
                    type.fsid,
                    type.server_type);
        }
        rc = mdb_cursor_get(cur, &key, &val, MDB_NEXT);
    }
    mdb_cursor_close(cur);
    mdb_txn_abort(txn);
    return 0;
}

static int sid_read_all(FILE *inp)
{
    char kind[16];
    char uuid[SID_STR_LEN];
    char url[256];
    char banner[32];
    int version = 0;
    int attrs[SID_NUM_ATTR];
    long long bmi;
    int i;
    int rc;

    if (fscanf(inp, "%31s %d", banner, &version) != 2 ||
        strcmp(banner, "SIDCACHE") != 0 || version != 1)
    {
        return -PVFS_EINVAL;
    }
    while (fscanf(inp, "%15s", kind) == 1)
    {
        if (strcmp(kind, "S") == 0)
        {
            PVFS_SID sid;
            SID_cacheval_t *cval = NULL;
            if (fscanf(inp, "%36s %lld %255s", uuid, &bmi, url) != 3)
            {
                return -PVFS_EINVAL;
            }
            for (i = 0; i < SID_NUM_ATTR; i++)
            {
                if (fscanf(inp, "%d", &attrs[i]) != 1)
                {
                    return -PVFS_EINVAL;
                }
            }
            if (PVFS_SID_str2bin(uuid, &sid) != 0)
            {
                return -PVFS_EINVAL;
            }
            rc = SID_cacheval_alloc(&cval, attrs, (BMI_addr)bmi, url);
            if (rc)
            {
                return rc;
            }
            rc = sid_put_u(&sid, cval, 0);
            SID_cacheval_free(&cval);
            if (rc)
            {
                return rc;
            }
        }
        else if (strcmp(kind, "T") == 0)
        {
            PVFS_SID sid;
            struct SID_type_s type;
            unsigned int typebits = 0;
            if (fscanf(inp, "%36s %d %u", uuid, &type.fsid, &typebits) != 3)
            {
                return -PVFS_EINVAL;
            }
            if (PVFS_SID_str2bin(uuid, &sid) != 0)
            {
                return -PVFS_EINVAL;
            }
            type.server_type = typebits;
            rc = sid_type_put_u(&sid, type);
            if (rc)
            {
                return rc;
            }
        }
        else
        {
            return -PVFS_EINVAL;
        }
    }
    return 0;
}

int SID_load(const char *path)
{
    FILE *inp;
    int rc;

    if (!path)
    {
        return 0;
    }
    inp = fopen(path, "r");
    if (!inp)
    {
        if (errno == ENOENT)
        {
            return 0;
        }
        return -PVFS_EIO;
    }
    sid_lock();
    rc = sid_read_all(inp);
    sid_unlock();
    fclose(inp);
    return rc;
}

int SID_loadbuffer(const char *buffer, int size)
{
    FILE *inp;
    int rc;

    if (!buffer || size < 0)
    {
        return -PVFS_EINVAL;
    }
    inp = fmemopen((void *)buffer, (size_t)size, "r");
    if (!inp)
    {
        return -PVFS_EIO;
    }
    sid_lock();
    rc = sid_read_all(inp);
    sid_unlock();
    fclose(inp);
    return rc;
}

int SID_save(const char *path)
{
    FILE *out;
    int rc;

    if (!path)
    {
        return -PVFS_EINVAL;
    }
    out = fopen(path, "w");
    if (!out)
    {
        return -PVFS_EIO;
    }
    sid_lock();
    rc = sid_write_all(out);
    sid_unlock();
    fclose(out);
    return rc;
}

int SID_savelist(char *buffer, int size, SID_server_list_t *slist)
{
    FILE *out;
    struct qlist_head *pos;

    if (!buffer || size <= 0 || !slist)
    {
        return -PVFS_EINVAL;
    }
    out = fmemopen(buffer, (size_t)size, "w");
    if (!out)
    {
        return -PVFS_EIO;
    }
    qlist_for_each(pos, &slist->link)
    {
        SID_server_list_t *server = qlist_entry(pos, SID_server_list_t, link);
        fprintf(out, "%s %lld %s\n",
                PVFS_SID_str(&server->server_sid),
                (long long)server->server_addr,
                server->server_url ? server->server_url : "-");
    }
    fclose(out);
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
