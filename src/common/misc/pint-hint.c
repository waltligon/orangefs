/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#define __PINT_REQPROTO_ENCODE_FUNCS_C

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <string.h>
#include "pint-hint.h"
#include "gossip.h"
#include <stdio.h>
#include <pvfs2-debug.h>

DEFINE_STATIC_ENDECODE_FUNCS(uint64_t, uint64_t);
DEFINE_STATIC_ENDECODE_FUNCS(int64_t, int64_t);
DEFINE_STATIC_ENDECODE_FUNCS(uint32_t, uint32_t);
DEFINE_STATIC_ENDECODE_FUNCS(int32_t, int32_t);
DEFINE_STATIC_ENDECODE_FUNCS(string, char *);

struct PINT_hint_info
{
    enum PINT_hint_type type;
    int flags;
    const char * name;
    void (*encode)(char **pptr, void *value);
    void (*decode)(char **pptr, void *value);
    int length;
};

static int PINT_hint_check(PVFS_hint *hints, enum PINT_hint_type type);

static const struct PINT_hint_info hint_types[] = {

    {PINT_HINT_REQUEST_ID,
     PINT_HINT_TRANSFER,
     PVFS_HINT_REQUEST_ID_NAME,
     encode_func_uint32_t,
     decode_func_uint32_t,
     sizeof(uint32_t)},

    {PINT_HINT_CLIENT_ID,
     PINT_HINT_TRANSFER,
     PVFS_HINT_CLIENT_ID_NAME,
     encode_func_uint32_t,
     decode_func_uint32_t,
     sizeof(uint32_t)},

    {PINT_HINT_HANDLE,
     PINT_HINT_TRANSFER,
     PVFS_HINT_HANDLE_NAME,
     encode_func_uint64_t,
     decode_func_uint64_t,
     sizeof(PVFS_handle)},

    {PINT_HINT_OP_ID,
     0,
     PVFS_HINT_OP_ID_NAME,
     encode_func_uint32_t,
     decode_func_uint32_t,
     sizeof(uint32_t)},

    {PINT_HINT_RANK,
     PINT_HINT_TRANSFER,
     PVFS_HINT_RANK_NAME,
     encode_func_uint32_t,
     decode_func_uint32_t,
     sizeof(uint32_t)},

    {PINT_HINT_SERVER_ID,
     PINT_HINT_TRANSFER,
     PVFS_HINT_SERVER_ID_NAME,
     encode_func_uint32_t,
     decode_func_uint32_t,
     sizeof(uint32_t)},

    {0}
};

static const struct PINT_hint_info *PINT_hint_get_info_by_type(int type)
{
    int j = 0;
    while(hint_types[j].type != 0)
    {
        if(type == hint_types[j].type)
        {
            return &hint_types[j];
        }
        ++j;
    }

    return NULL;
}

static const struct PINT_hint_info *
PINT_hint_get_info_by_name(const char *name)
{
    int j = 0;
    while(hint_types[j].type != 0)
    {
        if(!strcmp(name, hint_types[j].name))
        {
            return &hint_types[j];
        }
        ++j;
    }
    return NULL;
}

int PVFS_hint_add_internal(
    PVFS_hint *hint,
    enum PINT_hint_type type,
    int length,
    void *value)
{
    int ret;
    const struct PINT_hint_info *info;

    info = PINT_hint_get_info_by_type(type);
    if(info)
    {
        ret = PINT_hint_check(hint, info->type);
        if(ret == -PVFS_EEXIST)
        {
            return PVFS_hint_replace_internal(hint, type, length, value);
        }
    }

    PINT_hint * new_hint = malloc(sizeof(PINT_hint));
    if (!new_hint)
    {
        return -PVFS_ENOMEM;
    }

    new_hint->length = length;
    new_hint->value = malloc(new_hint->length);
    if(!new_hint->value)
    {
        free(new_hint);
        return -PVFS_ENOMEM;
    }

    memcpy(new_hint->value, value, length);

    info = PINT_hint_get_info_by_type(type);
    if(info)
    {
        new_hint->type = info->type;
        new_hint->flags = info->flags;
        new_hint->encode = info->encode;
        new_hint->decode = info->decode;
    }

    new_hint->next = *hint;
    *hint = new_hint;

    return 0;
}

int PVFS_hint_replace(
    PVFS_hint *hint,
    const char *type,
    int length,
    void *value)
{
    const struct PINT_hint_info *info;

    info = PINT_hint_get_info_by_name(type);
    if(info)
    {
        return PVFS_hint_replace_internal(hint, info->type, length, value);
    }
    return PVFS_hint_add(hint, type, length, value);
}

int PVFS_hint_replace_internal(
    PVFS_hint *hint,
    enum PINT_hint_type type,
    int length,
    void *value)
{
    PINT_hint *tmp;
    const struct PINT_hint_info *info;

    info = PINT_hint_get_info_by_type(type);
    if(info)
    {
        tmp = *hint;
        while(tmp)
        {
            if(tmp->type == info->type)
            {
                free(tmp->value);
                tmp->length = length;
                tmp->value = malloc(length);
                if(!tmp->value)
                {
                    return -PVFS_ENOMEM;
                }
                memcpy(tmp->value, value, length);
                return 0;
            }

            tmp = tmp->next;
        }
    }
    return -PVFS_ENOENT;
}

int PVFS_hint_add(
    PVFS_hint *hint,
    const char *type,
    int length,
    void *value)
{
    int ret;
    const struct PINT_hint_info *info;

    info = PINT_hint_get_info_by_name(type);
    if(info)
    {
        ret = PINT_hint_check(hint, info->type);
        if(ret == -PVFS_EEXIST)
        {
            return ret;
        }
    }

    PINT_hint * new_hint = malloc(sizeof(PINT_hint));
    if (!new_hint)
    {
        return -PVFS_ENOMEM;
    }

    new_hint->length = length;
    new_hint->value = malloc(new_hint->length);
    if(!new_hint->value)
    {
        free(new_hint);
        return -PVFS_ENOMEM;
    }

    memcpy(new_hint->value, value, length);

    if(info)
    {
        new_hint->type = info->type;
        new_hint->flags = info->flags;
        new_hint->encode = info->encode;
        new_hint->decode = info->decode;
    }
    else
    {
        new_hint->type = PINT_HINT_UNKNOWN;
        new_hint->type_string = strdup(type);

        /* always transfer unknown hints */
        new_hint->flags = PINT_HINT_TRANSFER;
        new_hint->encode = encode_func_string;
        new_hint->decode = decode_func_string;
    }

    new_hint->next = *hint;
    *hint = new_hint;

    return 0;
}

int PVFS_hint_check(PVFS_hint *hints, const char *type)
{
    const struct PINT_hint_info *info;

    info = PINT_hint_get_info_by_name(type);
    return PINT_hint_check(hints, info->type);
}

static int PINT_hint_check(PVFS_hint *hints, enum PINT_hint_type type)
{
    PINT_hint *tmp;

    if(!hints) return 0;

    tmp = *hints;
    while(tmp)
    {
        if(tmp->type == type)
        {
            return -PVFS_EEXIST;
        }
        tmp = tmp->next;
    }
    return 0;
}

void encode_PINT_hint(char **pptr, const PINT_hint *hint)
{
    int transfer_count = 0;
    const PINT_hint *tmp_hint = hint;

    /* count up the transferable hints */
    while(tmp_hint)
    {
        if(tmp_hint->flags & PINT_HINT_TRANSFER)
        {
            transfer_count++;
        }

        tmp_hint = tmp_hint->next;
    }

    /* encode the number of hints to be transferred */
    encode_uint32_t(pptr, &transfer_count);

    tmp_hint = hint;
    while(tmp_hint)
    {
        /* encode the hint type */
        if(tmp_hint->flags & PINT_HINT_TRANSFER)
        {
            encode_uint32_t(pptr, &tmp_hint->type);

            /* if the type is unknown, encode the type string */
            if(tmp_hint->type == PINT_HINT_UNKNOWN)
            {
                encode_string(pptr, &tmp_hint->type_string);
            }

            /* encode the hint using the encode function provided */
            tmp_hint->encode(pptr, tmp_hint->value);
        }

        tmp_hint = tmp_hint->next;
    }
}

void decode_PINT_hint(char **pptr, PINT_hint **hint)
{
    int count, i, type;
    PINT_hint *new_hint = NULL;
    const struct PINT_hint_info *info;

    decode_uint32_t(pptr, &count);

    gossip_debug(GOSSIP_SERVER_DEBUG, "decoding %d hints from request\n",
                 count);

    for(i = 0; i < count; ++i)
    {
        decode_uint32_t(pptr, &type);
        info = PINT_hint_get_info_by_type(type);
        if(info)
        {
            char *start;
            int len;
            void *value = malloc(info->length);
            if(!value)
            {
                    return;
            }

            start = *pptr;
            info->decode(pptr, value);
            len = (*pptr - start);
            PVFS_hint_add(&new_hint, info->name, len, value);
            free(value);
        }
        else
        {
            char *type_string;
            char *value;
            /* not a recognized hint, assume its a string */
            decode_string(pptr, &type_string);
            decode_string(pptr, &value);
            PVFS_hint_add(&new_hint, type_string, strlen(value) + 1, value);
        }
    }

    *hint = new_hint;
}

int PVFS_hint_copy(PVFS_hint old_hint, PVFS_hint *new_hint)
{
    const struct PINT_hint_info *info;
    PINT_hint *h = old_hint;
    const char *name;

    if(!old_hint)
    {
        *new_hint = NULL;
        return 0;
    }

    while(h)
    {
        info = PINT_hint_get_info_by_type(h->type);
        if(!info)
        {
            name = h->type_string;
        }
        else
        {
            name = info->name;
        }

        PVFS_hint_add(new_hint, name, h->length, h->value);
        h = h->next;
    }
    return 0;
}

void PVFS_hint_free(PVFS_hint hint)
{
    PINT_hint * act = hint;
    PINT_hint * old;

    while(act != NULL)
    {
        old = act;
        act = act->next;

        free(old->value);

        if(old->type == PINT_HINT_UNKNOWN)
        {
            free(old->type_string);
        }
        free(old);
    }
}

/*
 * example environment variable
 * PVFS2_HINTS =
 *'pvfs.hint.request_id:10+pvfs.hint.client_id:30'
 */
int PVFS_hint_import_env(PVFS_hint * out_hint)
{
    char * env;
    char * env_copy;
    char * save_ptr = NULL;
    char * aktvar;
    char name[PVFS_HINT_MAX_NAME_LENGTH];
    int len;
    const struct PINT_hint_info *info;
    PINT_hint *hint = NULL;
    int ret;

    if( out_hint == NULL )
    {
        return 1;
    }
    env = getenv("PVFS2_HINTS");
    if( env == NULL )
    {
        return 0;
    }
    len = strlen(env);
    env_copy = (char *) malloc(sizeof(char) * (len+1));
    strncpy(env_copy, env, len+1);

    /* parse hints and do not overwrite already specified hints !*/
    aktvar = strtok_r(env_copy, "+", & save_ptr);
    while( aktvar != NULL )
    {
        char * rest;

        rest = index(aktvar, ':');
        if (rest == NULL)
        {
            gossip_err("Environment variable PVFS2_HINTS is "
                       "malformed starting with: %s\n",
                       aktvar);
            free(env_copy);
            return 0;
        }

        *rest = 0;

        sprintf(name, "pvfs2.hint.%s", aktvar);
        info = PINT_hint_get_info_by_name(name);
        if(info)
        {
            /* a bit of a hack..if we know the type and its
             * an int, we convert from a string
             */
            if(info->encode == encode_func_uint32_t)
            {
                uint32_t val;
                sscanf(rest+1, "%u", &val);
                ret = PVFS_hint_add(&hint, info->name, sizeof(val), &val);
            }
            else if(info->encode == encode_func_uint64_t)
            {
                uint32_t val;
                sscanf(rest+1, "%u", &val);
                ret = PVFS_hint_add(&hint, info->name, sizeof(val), &val);
            }
            else if(info->encode == encode_func_string)
            {
                /* just pass the string along as the hint value */
                ret = PVFS_hint_add(&hint, info->name, strlen(rest+1), rest+1);
            }
            else
            {
                /* Can't specify a complex hint in the PVFS2_HINTS environment
                 * variable.
                 */
                ret = -PVFS_EINVAL;
            }
        }
        else
        {
            /* Hint not recognized, so we store it with its name */
            ret = PVFS_hint_add(&hint, name, strlen(rest+1), rest+1);
        }

        if(ret < 0)
        {
            /* hint parsing failed */
            PVFS_hint_free(hint);
            free(env_copy);
            return ret;
        }

        aktvar = strtok_r(NULL, "+", & save_ptr);
    }

    free(env_copy);
    return 0;
}

void *PINT_hint_get_value_by_type(
    struct PVFS_hint_s *hint, enum PINT_hint_type type, int *length)
{
    PINT_hint *h;

    h = hint;

    while(h)
    {
        if(h->type == type)
        {
            if(length)
            {
                *length = h->length;
            }
            return h->value;
        }

        h = h->next;
    }
    return NULL;
}

void *PINT_hint_get_value_by_name(
    struct PVFS_hint_s *hint, const char *name, int *length)
{
    PINT_hint *h;

    h = hint;

    while(h)
    {
        if(!strcmp(h->type_string, name))
        {
            if(length)
            {
                *length = h->length;
            }
            return h->value;
        }

        h = h->next;
    }
    return NULL;
}

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
