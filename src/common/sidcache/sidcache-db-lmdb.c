/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 *
 * LMDB backend for sidcache-db.h. The shape of the functions follows
 * src/io/trove/trove-dbpf/dbpf-db-lmdb.c.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <lmdb.h>

#include "pvfs2-internal.h"
#include "gen-locks.h"
#include "pvfs2-types.h"
#include "sidcacheval.h"
#include "sidcache-db.h"

/* Map size for this environment. CACHE_SIZE_MB in sidcache.h is the
 * old Berkeley cache setting and does not change this.
 */
#define SID_DB_MAPSIZE ((size_t)32 * 1024 * 1024)
#define SID_DB_MAXDBS 16

struct sid_env
{
    MDB_env *env;
    MDB_txn *txn;
};

struct sid_db
{
    sid_env *env;
    MDB_dbi dbi;
};

struct sid_cursor
{
    sid_env *env;
    MDB_cursor *cursor;
    MDB_txn *txn;
    int own_txn;
};

static gen_mutex_t sid_db_mu;
static int sid_db_mu_ready = 0;

static void sid_db_lock(void)
{
    if (sid_db_mu_ready)
    {
        gen_mutex_lock(&sid_db_mu);
    }
}

static void sid_db_unlock(void)
{
    if (sid_db_mu_ready)
    {
        gen_mutex_unlock(&sid_db_mu);
    }
}

static int sid_db_error(int rc)
{
    if (rc == 0)
    {
        return 0;
    }
    if (rc == MDB_NOTFOUND)
    {
        return -PVFS_ENOENT;
    }
    if (rc == MDB_KEYEXIST)
    {
        return -PVFS_EEXIST;
    }
    return -PVFS_EIO;
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

static int take_bytes(const MDB_val *src, struct sid_data *dst)
{
    void *copy;

    copy = malloc(src->mv_size ? src->mv_size : 1);
    if (!copy)
    {
        return -PVFS_ENOMEM;
    }
    if (src->mv_size)
    {
        memcpy(copy, src->mv_data, src->mv_size);
    }
    dst->data = copy;
    dst->len = src->mv_size;
    return 0;
}

int sid_db_env_open(const char *path, sid_env **env)
{
    sid_env *out;
    int rc;

    if (!path || !env)
    {
        return -PVFS_EINVAL;
    }
    out = (sid_env *)calloc(1, sizeof(*out));
    if (!out)
    {
        return -PVFS_ENOMEM;
    }
    if (!sid_db_mu_ready)
    {
        gen_mutex_init(&sid_db_mu);
        sid_db_mu_ready = 1;
    }
    rc = mdb_env_create(&out->env);
    if (rc)
    {
        free(out);
        return sid_db_error(rc);
    }
    mdb_env_set_maxdbs(out->env, SID_DB_MAXDBS);
    rc = mdb_env_set_mapsize(out->env, SID_DB_MAPSIZE);
    if (rc)
    {
        mdb_env_close(out->env);
        free(out);
        return sid_db_error(rc);
    }
    rc = mdb_env_open(out->env, path, MDB_NOSYNC, 0700);
    if (rc)
    {
        mdb_env_close(out->env);
        free(out);
        return sid_db_error(rc);
    }
    *env = out;
    return 0;
}

int sid_db_env_close(sid_env *env)
{
    if (!env)
    {
        return 0;
    }
    if (env->txn)
    {
        sid_db_txn_abort(env);
    }
    sid_db_lock();
    mdb_env_close(env->env);
    sid_db_unlock();
    free(env);
    return 0;
}

int sid_db_txn_begin(sid_env *env, int rdonly)
{
    int rc;

    if (!env || env->txn)
    {
        return -PVFS_EINVAL;
    }
    sid_db_lock();
    rc = mdb_txn_begin(env->env, NULL, rdonly ? MDB_RDONLY : 0, &env->txn);
    if (rc)
    {
        sid_db_unlock();
        return sid_db_error(rc);
    }
    return 0;
}

int sid_db_txn_commit(sid_env *env)
{
    int rc;

    if (!env || !env->txn)
    {
        return -PVFS_EINVAL;
    }
    rc = mdb_txn_commit(env->txn);
    env->txn = NULL;
    sid_db_unlock();
    return sid_db_error(rc);
}

int sid_db_txn_abort(sid_env *env)
{
    if (!env || !env->txn)
    {
        return -PVFS_EINVAL;
    }
    mdb_txn_abort(env->txn);
    env->txn = NULL;
    sid_db_unlock();
    return 0;
}

int sid_db_open(sid_env *env, const char *name, int compare, int flags,
                sid_db **db)
{
    sid_db *out;
    MDB_txn *txn = NULL;
    int own = 0;
    unsigned int oflags = MDB_CREATE;
    int rc;

    if (!env || !name || !db)
    {
        return -PVFS_EINVAL;
    }
    out = (sid_db *)calloc(1, sizeof(*out));
    if (!out)
    {
        return -PVFS_ENOMEM;
    }
    out->env = env;
    if (env->txn)
    {
        txn = env->txn;
    }
    else
    {
        rc = sid_db_txn_begin(env, 0);
        if (rc)
        {
            free(out);
            return rc;
        }
        txn = env->txn;
        own = 1;
    }
    if (flags & SID_DB_DUP)
    {
        oflags |= MDB_DUPSORT;
    }
    rc = mdb_dbi_open(txn, name, oflags, &out->dbi);
    if (rc)
    {
        free(out);
        if (own)
        {
            sid_db_txn_abort(env);
        }
        return sid_db_error(rc);
    }
    if (compare == SID_DB_COMPARE_TYPE)
    {
        mdb_set_compare(txn, out->dbi, type_cmp);
    }
    if (own)
    {
        rc = sid_db_txn_commit(env);
        if (rc)
        {
            free(out);
            return rc;
        }
    }
    *db = out;
    return 0;
}

static MDB_txn *active_txn(sid_db *db, int rdonly, int *own)
{
    int rc;

    *own = 0;
    if (db->env->txn)
    {
        return db->env->txn;
    }
    rc = mdb_txn_begin(db->env->env, NULL, rdonly ? MDB_RDONLY : 0, &db->env->txn);
    if (rc)
    {
        return NULL;
    }
    *own = 1;
    return db->env->txn;
}

static void finish_own(sid_db *db, int own, int commit, int rc)
{
    if (!own)
    {
        return;
    }
    if (commit && rc == 0)
    {
        mdb_txn_commit(db->env->txn);
    }
    else
    {
        mdb_txn_abort(db->env->txn);
    }
    db->env->txn = NULL;
    sid_db_unlock();
}

int sid_db_get(sid_db *db, struct sid_data *key, struct sid_data *val)
{
    MDB_txn *txn;
    MDB_val k;
    MDB_val v;
    int own = 0;
    int rc;

    if (!db || !key || !val)
    {
        return -PVFS_EINVAL;
    }
    if (!db->env->txn)
    {
        sid_db_lock();
    }
    txn = active_txn(db, 1, &own);
    if (!txn)
    {
        if (own || !db->env->txn)
        {
            sid_db_unlock();
        }
        return -PVFS_EIO;
    }
    k.mv_data = key->data;
    k.mv_size = key->len;
    rc = mdb_get(txn, db->dbi, &k, &v);
    if (rc == 0)
    {
        rc = take_bytes(&v, val);
        finish_own(db, own, 0, 0);
        return rc;
    }
    finish_own(db, own, 0, rc);
    return sid_db_error(rc);
}

static int put_flags(sid_db *db, struct sid_data *key, struct sid_data *val,
                     unsigned int flags)
{
    MDB_txn *txn;
    MDB_val k;
    MDB_val v;
    int own = 0;
    int rc;

    if (!db || !key || !val)
    {
        return -PVFS_EINVAL;
    }
    if (!db->env->txn)
    {
        sid_db_lock();
    }
    txn = active_txn(db, 0, &own);
    if (!txn)
    {
        sid_db_unlock();
        return -PVFS_EIO;
    }
    k.mv_data = key->data;
    k.mv_size = key->len;
    v.mv_data = val->data;
    v.mv_size = val->len;
    rc = mdb_put(txn, db->dbi, &k, &v, flags);
    if (flags == MDB_NODUPDATA && rc == MDB_KEYEXIST)
    {
        rc = 0;
    }
    finish_own(db, own, 1, rc);
    return sid_db_error(rc);
}

int sid_db_put(sid_db *db, struct sid_data *key, struct sid_data *val)
{
    return put_flags(db, key, val, 0);
}

int sid_db_putonce(sid_db *db, struct sid_data *key, struct sid_data *val)
{
    return put_flags(db, key, val, MDB_NOOVERWRITE);
}

int sid_db_putdup(sid_db *db, struct sid_data *key, struct sid_data *val)
{
    return put_flags(db, key, val, MDB_NODUPDATA);
}

int sid_db_del(sid_db *db, struct sid_data *key, struct sid_data *val)
{
    MDB_txn *txn;
    MDB_val k;
    MDB_val v;
    int own = 0;
    int rc;

    if (!db || !key)
    {
        return -PVFS_EINVAL;
    }
    if (!db->env->txn)
    {
        sid_db_lock();
    }
    txn = active_txn(db, 0, &own);
    if (!txn)
    {
        sid_db_unlock();
        return -PVFS_EIO;
    }
    k.mv_data = key->data;
    k.mv_size = key->len;
    if (val)
    {
        v.mv_data = val->data;
        v.mv_size = val->len;
        rc = mdb_del(txn, db->dbi, &k, &v);
    }
    else
    {
        rc = mdb_del(txn, db->dbi, &k, NULL);
    }
    finish_own(db, own, 1, rc);
    return sid_db_error(rc);
}

int sid_db_cursor(sid_db *db, sid_cursor **dbc, int rdonly)
{
    sid_cursor *out;
    int rc;

    if (!db || !dbc)
    {
        return -PVFS_EINVAL;
    }
    out = (sid_cursor *)calloc(1, sizeof(*out));
    if (!out)
    {
        return -PVFS_ENOMEM;
    }
    out->env = db->env;
    if (db->env->txn)
    {
        out->txn = db->env->txn;
        out->own_txn = 0;
    }
    else
    {
        sid_db_lock();
        rc = mdb_txn_begin(db->env->env, NULL,
                           rdonly ? MDB_RDONLY : 0, &out->txn);
        if (rc)
        {
            sid_db_unlock();
            free(out);
            return sid_db_error(rc);
        }
        out->own_txn = 1;
    }
    rc = mdb_cursor_open(out->txn, db->dbi, &out->cursor);
    if (rc)
    {
        if (out->own_txn)
        {
            mdb_txn_abort(out->txn);
            sid_db_unlock();
        }
        free(out);
        return sid_db_error(rc);
    }
    *dbc = out;
    return 0;
}

int sid_db_cursor_close(sid_cursor *dbc)
{
    if (!dbc)
    {
        return 0;
    }
    mdb_cursor_close(dbc->cursor);
    if (dbc->own_txn)
    {
        mdb_txn_abort(dbc->txn);
        sid_db_unlock();
    }
    free(dbc);
    return 0;
}

int sid_db_cursor_get(sid_cursor *dbc, struct sid_data *key,
                      struct sid_data *val, int op)
{
    MDB_val k;
    MDB_val v;
    int mop;
    int rc;
    int out_key = 0;

    if (!dbc || !key || !val)
    {
        return -PVFS_EINVAL;
    }
    memset(&k, 0, sizeof(k));
    memset(&v, 0, sizeof(v));
    switch (op)
    {
    case SID_DB_CURSOR_FIRST:
        mop = MDB_FIRST;
        out_key = 1;
        break;
    case SID_DB_CURSOR_NEXT:
        mop = MDB_NEXT;
        out_key = 1;
        break;
    case SID_DB_CURSOR_NEXT_DUP:
        mop = MDB_NEXT_DUP;
        break;
    case SID_DB_CURSOR_CURRENT:
        mop = MDB_GET_CURRENT;
        out_key = 1;
        break;
    case SID_DB_CURSOR_SET:
        k.mv_data = key->data;
        k.mv_size = key->len;
        mop = MDB_SET;
        break;
    case SID_DB_CURSOR_SET_RANGE:
        k.mv_data = key->data;
        k.mv_size = key->len;
        mop = MDB_SET_RANGE;
        out_key = 1;
        break;
    case SID_DB_CURSOR_GET_BOTH:
        k.mv_data = key->data;
        k.mv_size = key->len;
        v.mv_data = val->data;
        v.mv_size = val->len;
        mop = MDB_GET_BOTH;
        break;
    default:
        return -PVFS_EINVAL;
    }
    rc = mdb_cursor_get(dbc->cursor, &k, &v, mop);
    if (rc)
    {
        return sid_db_error(rc);
    }
    if (op == SID_DB_CURSOR_GET_BOTH)
    {
        return 0;
    }
    if (out_key)
    {
        rc = take_bytes(&k, key);
        if (rc)
        {
            return rc;
        }
    }
    return take_bytes(&v, val);
}

int sid_db_cursor_count(sid_cursor *dbc, size_t *count)
{
    int rc;

    if (!dbc || !count)
    {
        return -PVFS_EINVAL;
    }
    rc = mdb_cursor_count(dbc->cursor, count);
    return sid_db_error(rc);
}
