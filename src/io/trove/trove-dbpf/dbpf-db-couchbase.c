/*
 * Copyright 2015 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <gossip.h>

#include <libcouchbase/couchbase.h>
#include <libcouchbase/views.h>

#include "dbpf.h"

#include "server-config.h"

struct dbpf_db {
    lcb_t instance;
};

struct dbpf_cursor {
    struct dbpf_db *db;
    struct dbpf_data lastkey;
};

struct cookie {
    lcb_error_t rc;
    struct dbpf_data *key;
    struct dbpf_data *val;
    int op;
};

static int copy(struct dbpf_data *dest, struct dbpf_data *src)
{
    dest->len = src->len;
    dest->data = malloc(dest->len);
    if (!dest->data)
    {
        return -1;
    }
    memcpy(dest->data, src->data, dest->len);
    return 0;
}

static char *hex_create(unsigned char *data, size_t *len)
{
    size_t strlen, i, j;
    char *str;
    strlen = 2**len + 1;
    /* extra byte is not needed but snprintf won't write without it */
    str = malloc(strlen);
    if (!str)
    {
        return NULL;
    }
    j = 0;
    for (i = 0; i < *len; i++)
    {
        int out;
        out = snprintf(str + j, strlen - j, "%x", data[i] & 0xf);
        j += out;
        out = snprintf(str + j, strlen - j, "%x", (data[i] >> 4) & 0xf);
        j += out;
    }
    *len = j;
    return str;
}

static unsigned char *hex_parse(const char *str, size_t *strlen)
{
    unsigned char *data;
    size_t i, len;
    char buf[3];
    len = *strlen/2;
    data = malloc(len);
    if (!data)
    {
        return NULL;
    }
    buf[0] = 0;
    for (i = 0; i < len; i++)
    {
        char x;
        memcpy(buf, str+2*i, 2);
        x = buf[0];
        buf[0] = buf[1];
        buf[1] = x;
        data[i] = strtol(buf, NULL, 16);
    }
    *strlen = len;
    return data;
}

static char *json_create(unsigned char *data, size_t *len)
{
    size_t strlen, i, j;
    char *str;
    /* opening [ plus three digit number plus , where the last , becomes
     * the closing ] */
    strlen = 1 + 4**len;
    str = malloc(strlen);
    if (!str)
    {
        return NULL;
    }
    j = 0;
    str[j++] = '[';
    for (i = 0; i < *len; i++)
    {
        int out;
        out = snprintf(str + j, strlen - j, "%d,", data[i]);
        j += out;
    }
    str[j-1] = ']';
    strlen = j;
    *len = strlen;
    return str;
}

static unsigned char *json_parse(const char *str, size_t *strlen)
{
    unsigned char *data;
    size_t i, j, len;
    const char *s;
    if (*strlen == 0 || str[0] != '[')
    {
        return NULL;
    }
    len = 0;
    for (i = 1; i < *strlen; i++)
    {
        if (str[i] == ']')
        {
            break;
        }
        else if (str[i] != ',' && (str[i] < '0' || str[i] > '9'))
        {
            return NULL;
        }
        else if (str[i] == ',')
        {
            len++;
        }
    }
    if (str[i] != ']')
    {
        return NULL;
    }
    if (len)
    {
        len++;
    }
    data = malloc(len);
    if (!data)
    {
        return NULL;
    }
    s = str + 1;
    for (j = 0; j < len; j++ )
    {
        char *end;
        data[j] = strtol(s, &end, 10);
        s = end + 1;
    }
    *strlen = len;
    return data;
}

static void callback(lcb_t instance, int cbtype, const lcb_RESPBASE *rb)
{
    const lcb_RESPGET *respget = (const lcb_RESPGET *)rb;
    struct cookie *c = (struct cookie *)rb->cookie;
    if (rb->rc == LCB_SUCCESS)
    {
        switch (cbtype)
        {
        case LCB_CALLBACK_GET:
            {unsigned char *data;
            size_t len;
            len = respget->nvalue;
            data = json_parse(respget->value, &len);
            memcpy(c->val->data, data, len);
            free(data);
            c->val->len = len;}
        }
    }
    c->rc = rb->rc;
}

static void vqcallback(lcb_t instance, int cbtype, const lcb_RESPVIEWQUERY *row)
{
    struct cookie *c = (struct cookie *)row->cookie;
    if (!row->rflags & LCB_RESP_F_FINAL)
    {
        unsigned char *data;
        size_t len;

/* XXX when is c->key set?
 * XXX data return value (and in callback above)
 * XXX maxkeylen?? */

        if (c->key)
        {
            len = row->nkey - 1;
            data = hex_parse(row->key + 1, &len);
            memcpy(c->key->data, data, c->key->len);
            free(data);
            c->key->len = len;
        }
        len = row->nvalue;
        data = json_parse(row->value, &len);
        memcpy(c->val->data, data, c->val->len);
        free(data);
        c->val->len = len;
        c->rc = row->rc;
    }
}

int dbpf_db_open(char *name, int compare, struct dbpf_db **db,
    int create, struct server_configuration_s *cfg, char *collname)
{
    struct lcb_create_st create_options = {0};
    char connstr[256], *s;
    lcb_error_t err;

    *db = malloc(sizeof **db);
    if (!*db)
    {
        return TROVE_ENOMEM;
    }

    /* XXX */
    s = strrchr(name, '/');
    if (s == NULL)
    {
        return TROVE_EINVAL;
    }
    if (!strcmp(s+1, ""))
    {
        return TROVE_EINVAL;
    }
    if (snprintf(connstr, sizeof connstr, "couchbase://127.0.0.1/%s%s%s-%s",
            cfg->server_alias, collname ? "-" : "", collname ? collname : "",
            s+1) > sizeof connstr-1)
    {
        return TROVE_ENOMEM;
    }

    /*gossip_err("%p %s\n", *db, connstr);*/

    create_options.version = 3;
    create_options.v.v3.connstr = connstr;

    /* XXX: all these errors will have to go into a wrapper
    http://docs.couchbase.com/sdk-api/couchbase-c-client-2.6.2/group__lcb-error-codes.html#gac1f5be170e51b1bfbe1befb886cc7173
     */

    err = lcb_create(&(*db)->instance, &create_options);
    if (err != LCB_SUCCESS)
    {
        free(*db);
        return TROVE_ENOMEM;
    }

    err = lcb_connect((*db)->instance);
    if (err != LCB_SUCCESS)
    {
        free(*db);
        return TROVE_ECONNREFUSED;
    }

    err = lcb_wait((*db)->instance);
    if (err == LCB_BUCKET_ENOENT)
    {
        gossip_err("dbpf_db_open: couchbase bucket does not exist %s\n",
                connstr);
        free(*db);
        return TROVE_ECONNREFUSED;
    }
    else if (err != LCB_SUCCESS)
    {
        free(*db);
        return TROVE_ECONNREFUSED;
    }

    err = lcb_get_bootstrap_status((*db)->instance);
    if (err != LCB_SUCCESS)
    {
        free(*db);
        return TROVE_ECONNREFUSED;
    }

    lcb_install_callback3((*db)->instance, LCB_CALLBACK_GET, callback);
    lcb_install_callback3((*db)->instance, LCB_CALLBACK_STORE, callback);
    lcb_install_callback3((*db)->instance, LCB_CALLBACK_REMOVE, callback);

    /* setup callbacks. each operation will need to wait with lcb_wait
     * then read data saved by callback */
    /* if multiple trove threads this won't work
     * but I think we're fine */
    /* I think setup is complete here anyway */

    return 0;
}

int dbpf_db_close(struct dbpf_db *db)
{
    lcb_error_t err;
    err = lcb_wait(db->instance);
    free(db);
    if (err != LCB_SUCCESS)
    {
        return TROVE_ECONNREFUSED;
    }
    return 0;
}

int dbpf_db_sync(struct dbpf_db *db)
{
    /* XXX */
    return 0;
}

int dbpf_db_get(struct dbpf_db *db, struct dbpf_data *key,
    struct dbpf_data *val)
{
    lcb_CMDGET cmd = {0};
    size_t keystrlen;
    lcb_error_t err;
    struct cookie c;
    char *keystr;
    c.val = val;
    keystrlen = key->len;
    keystr = hex_create(key->data, &keystrlen);
    if (!keystr)
    {
        return TROVE_ENOMEM;
    }
    LCB_CMD_SET_KEY(&cmd, keystr, keystrlen);
    err = lcb_get3(db->instance, &c, &cmd);
    if (err != LCB_SUCCESS)
    {
        return TROVE_ECONNREFUSED;
    }
    err = lcb_wait(db->instance);
    if (err != LCB_SUCCESS)
    {
        return TROVE_ECONNREFUSED;
    }
    free(keystr);
    switch (c.rc)
    {
    case LCB_SUCCESS:
        return 0;
    case LCB_KEY_ENOENT:
        return TROVE_ENOENT;
    default:
        gossip_err("dbpf_db_get: unknown lcb_error_t: %s (%d)\n",
            lcb_strcbtype(c.rc), c.rc);
        return DBPF_ERROR_UNKNOWN;
    }
}

static int put(struct dbpf_db *db, struct dbpf_data *key,
    struct dbpf_data *val, int type)
{
    size_t keystrlen, valstrlen;
    lcb_CMDSTORE cmd = {0};
    char *keystr, *valstr;
    lcb_error_t err;
    struct cookie c;
    keystrlen = key->len;
    keystr = hex_create(key->data, &keystrlen);
    if (!keystr)
    {
        return TROVE_ENOMEM;
    }
    valstrlen = val->len;
    valstr = json_create(val->data, &valstrlen);
    if (!valstr)
    {
        free(keystr);
        return TROVE_ENOMEM;
    }
    LCB_CMD_SET_KEY(&cmd, keystr, keystrlen);
    LCB_CMD_SET_VALUE(&cmd, valstr, valstrlen);
    cmd.operation = type;
    err = lcb_store3(db->instance, &c, &cmd);
    if (err != LCB_SUCCESS)
    {
        free(keystr);
        free(valstr);
        return TROVE_ECONNREFUSED;
    }
    err = lcb_wait(db->instance);
    if (err != LCB_SUCCESS)
    {
        free(keystr);
        free(valstr);
        return TROVE_ECONNREFUSED;
    }
    free(keystr);
    free(valstr);
    switch (c.rc)
    {
    case LCB_SUCCESS:
        return 0;
    default:
        gossip_err("dbpf_db_put: unknown lcb_error_t: %s (%d)\n",
            lcb_strcbtype(c.rc), c.rc);
        return DBPF_ERROR_UNKNOWN;
    }
}

int dbpf_db_put(struct dbpf_db *db, struct dbpf_data *key,
    struct dbpf_data *val)
{
    return put(db, key, val, LCB_SET);
}

int dbpf_db_putonce(struct dbpf_db *db, struct dbpf_data *key,
    struct dbpf_data *val)
{
    return put(db, key, val, LCB_ADD);
}

int dbpf_db_del(struct dbpf_db *db, struct dbpf_data *key)
{
    lcb_CMDREMOVE cmd = {0};
    size_t keystrlen;
    lcb_error_t err;
    struct cookie c;
    char *keystr;
    keystrlen = key->len;
    keystr = hex_create(key->data, &keystrlen);
    if (!keystr)
    {
        return TROVE_ENOMEM;
    }
    LCB_CMD_SET_KEY(&cmd, keystr, keystrlen);
    err = lcb_remove3(db->instance, &c, &cmd);
    if (err != LCB_SUCCESS)
    {
        return TROVE_ECONNREFUSED;
    }
    err = lcb_wait(db->instance);
    if (err != LCB_SUCCESS)
    {
        return TROVE_ECONNREFUSED;
    }
    free(keystr);
    switch (c.rc)
    {
    case LCB_SUCCESS:
        return 0;
    case LCB_KEY_ENOENT:
        return TROVE_ENOENT;
    default:
        gossip_err("dbpf_db_del: unknown lcb_error_t: %s (%d)\n",
            lcb_strcbtype(c.rc), c.rc);
        return DBPF_ERROR_UNKNOWN;
    }
}

int dbpf_db_cursor(struct dbpf_db *db, struct dbpf_cursor **dbc, int rdonly)
{
    (*dbc) = malloc(sizeof **dbc);
    if (!*dbc)
    {
        return TROVE_ENOMEM;
    }
    (*dbc)->db = db;
    (*dbc)->lastkey.data = NULL;
    return 0;
}

int dbpf_db_cursor_close(struct dbpf_cursor *dbc)
{
    if (dbc->lastkey.data)
    {
        free(dbc->lastkey.data);
    }
    free(dbc);
    return 0;
}

int dbpf_db_cursor_get(struct dbpf_cursor *dbc, struct dbpf_data *key,
    struct dbpf_data *val, int op, size_t maxkeylen)
{
    lcb_CMDVIEWQUERY cmd = {0};
    size_t startkeylen;
    char options[128];
    lcb_error_t err;
    struct cookie c;
    char *startkey;
    if (op != DBPF_DB_CURSOR_SET)
    {
        c.key = key;
    }
    else
    {
        c.key = NULL;
    }
    c.val = val;
    c.op = op;
    c.rc = -1;
    if (op == DBPF_DB_CURSOR_NEXT || op == DBPF_DB_CURSOR_CURRENT)
    {
        if (dbc->lastkey.data)
        {
            startkeylen = dbc->lastkey.len;
            startkey = hex_create(dbc->lastkey.data, &startkeylen);
            snprintf(options, sizeof options, "limit=1&%sstartkey=\"%s\"",
                    op == DBPF_DB_CURSOR_NEXT ? "skip=1&" : "",
                    startkey);
            free(startkey);
        }
        else
        {
            snprintf(options, sizeof options, "limit=1");
        }
    }
    else if (op == DBPF_DB_CURSOR_FIRST)
    {
        snprintf(options, sizeof options, "limit=1");
    }
    else
    {
        startkeylen = key->len;
        startkey = hex_create(key->data, &startkeylen);
        snprintf(options, sizeof options, "limit=1&startkey=\"%s\"", startkey);
        free(startkey);
    }
    lcb_view_query_initcmd(&cmd, "cursor", "cursor", options, vqcallback);
    err = lcb_view_query(dbc->db->instance, &c, &cmd);
    if (err != LCB_SUCCESS)
    {
        return TROVE_ECONNREFUSED;
    }
    err = lcb_wait(dbc->db->instance);
    if (err != LCB_SUCCESS)
    {
        return TROVE_ECONNREFUSED;
    }

    if (dbc->lastkey.data)
    {
        free(dbc->lastkey.data);
        dbc->lastkey.data = NULL;
    }
    if (c.rc != 0)
    {
        return c.rc == -1 ? TROVE_ENOENT : c.rc;
    }
    if (copy(&dbc->lastkey, key))
    {
        gossip_err("dbpf_db_cursor_get: no memory\n");
        return TROVE_ENOMEM;
    }
    return 0;
}

int dbpf_db_cursor_del(struct dbpf_cursor *dbc)
{
    if (dbc->lastkey.data)
    {
        return dbpf_db_del(dbc->db, &dbc->lastkey);
    }
    else
    {
        return TROVE_EINVAL;
    }
}
