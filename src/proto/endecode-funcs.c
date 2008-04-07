/*
 * (C) 2007 The University of Chicago.
 *
 * See COPYING in top-level directory.
 */
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C  /* trigger actual definitions */
#include "endecode-funcs.h"
#include <stdint.h>
#include "pvfs2-encode-stubs.h"

void encode_func_uint64_t(char **pptr, void *x) 
{
    encode_uint64_t(pptr, (uint64_t *)x);
}

void decode_func_uint64_t(char **pptr, void *x)
{
    decode_uint64_t(pptr, (uint64_t *)x);
}

void encode_func_int64_t(char **pptr, void *x)
{
    encode_int64_t(pptr, (int64_t *)x);
}

void decode_func_int64_t(char **pptr, void *x)
{
    decode_int64_t(pptr, (int64_t *)x);
}

void encode_func_uint32_t(char **pptr, void *x)
{
    encode_uint32_t(pptr, (uint32_t *)x);
}

void decode_func_uint32_t(char **pptr, void *x)
{
    decode_uint32_t(pptr, (uint32_t *)x);
}

void encode_func_int32_t(char **pptr, void *x)
{
    encode_int32_t(pptr, (int32_t *)x);
}

void decode_func_int32_t(char **pptr, void *x)
{
    decode_int32_t(pptr, (int32_t *)x);
}

void encode_func_string(char **pptr, void *x)
{
    encode_string(pptr, (char **)x);
}

void decode_func_string(char **pptr, void *x)
{
    decode_string(pptr, (char **)x);
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
