
#ifndef _PINT_REQUEST_ENCODE_H_
#define _PINT_REQUEST_ENCODE_H_

#ifndef __PINT_REQPROTO_ENCODE_FUNCS_C

inline static
int encode_PINT_Request(char ** pptr, 
                        struct PINT_Request ** rp)
{
    return 0;
}

inline static
int decode_PINT_Request(char ** pptr,
                        struct PINT_Request ** req)
{
    return 0;
}

#else

/* max depth of a PINT_Request used in anything, just servreq IO now */
#define PVFS_REQ_LIMIT_PINT_REQUEST_NUM  100

#include "gossip.h"
/* linearize the PINT_Request tree into a contiguous array */
inline static int 
linearize_PVFS_Request(
    struct PINT_Request * req, 
    struct PINT_Request ** linearized_req)
{
    int ret = 0;
    struct PINT_Request * linreq;
    /* XXX: This linearizes into a fresh buffer, encodes, then throws
     * it away; later fix the structure so that it is easier to
     * send. */

    if (req->num_nested_req > PVFS_REQ_LIMIT_PINT_REQUEST_NUM)
    {
        gossip_err("%s: demands %d elements,"
                   " more than maximum %d\n", __func__,
                   (req)->num_nested_req, PVFS_REQ_LIMIT_PINT_REQUEST_NUM);
        return -PVFS_EINVAL;
    }

    linreq = decode_malloc(
        (req->num_nested_req + 1) * sizeof(struct PINT_Request));
    if(!linreq)
    {
        return -PVFS_ENOMEM;
    }
    
    ret = PINT_request_commit(linreq, req);
    if(ret < 0)
    {
        free(linreq);
        return ret;
    }

    /* packs the pointers */
    ret = PINT_request_encode(linreq);
    if(ret < 0)
    {
        free(linreq);
        return ret;
    }
    
    *linearized_req = linreq;
    return ret;
}

inline static int 
encode_PVFS_Request_fields(
    char ** pptr,
    struct PINT_Request * rp)
{
    u_int32_t encti;
    encode_PVFS_offset(pptr, &(rp)->offset);
    encode_int32_t(pptr, &(rp)->num_ereqs);
    encode_int32_t(pptr, &(rp)->num_blocks);
    encode_PVFS_size(pptr, &(rp)->stride);
    encode_PVFS_offset(pptr, &(rp)->ub);
    encode_PVFS_offset(pptr, &(rp)->lb);
    encode_PVFS_size(pptr, &(rp)->aggregate_size);
    encode_int32_t(pptr, &(rp)->num_contig_chunks);
    encode_int32_t(pptr, &(rp)->depth);
    encode_int32_t(pptr, &(rp)->num_nested_req);
    encode_int32_t(pptr, &(rp)->committed);
    encode_int32_t(pptr, &(rp)->refcount);
    encode_skip4(pptr,);

    /* These pointers have been encoded already, just write as ints */
    encti = (u_int32_t)(uintptr_t) (rp)->ereq;
    encode_uint32_t(pptr, &encti);

    encti = (u_int32_t)(uintptr_t) (rp)->sreq;
    encode_uint32_t(pptr, &encti);

    return 0;
}

/* encode a linearized array of the above things, assumes space exists */
inline static int
encode_linearized_PVFS_Request(
    char ** pptr,
    struct PINT_Request * rp)
{
    int i, ret;
    for (i = 0; i <= rp->num_nested_req; i++) 
    {
        ret = encode_PVFS_Request_fields(pptr, (rp+i));
        if(ret < 0)
        {
            return ret;
        }
    }
    return 0;
}

inline static
int encode_PINT_Request(char ** pptr, 
                        struct PINT_Request ** rp)
{
    int ret;
    struct PINT_Request * linreq;

    ret = linearize_PVFS_Request(*rp, &linreq);
    if(ret < 0)
    {
        return ret;
    }

    encode_int32_t(pptr, &(*rp)->num_nested_req);
    encode_skip4(pptr,);
    
    ret = encode_linearized_PVFS_Request(pptr, linreq);
    decode_free(linreq);

    return ret;
}

inline static
int decode_PINT_Request(char ** pptr,
                        struct PINT_Request ** req)
{
    u_int32_t encti;
    int i;
    int numreq;
    struct PINT_Request * rp;

    decode_int32_t(pptr, &numreq);
    decode_skip4(pptr,);

    rp = decode_malloc((numreq + 1) * sizeof(struct PINT_Request));
    if(!rp)
    {
        return -PVFS_ENOMEM;
    }

    rp->num_nested_req = numreq;

    for (i = 0; i <= numreq; i++)
    {
        decode_PVFS_offset(pptr, &(rp+i)->offset);
        decode_int32_t(pptr, &(rp+i)->num_ereqs);
        decode_int32_t(pptr, &(rp+i)->num_blocks);
        decode_PVFS_size(pptr, &(rp+i)->stride);
        decode_PVFS_offset(pptr, &(rp+i)->ub);
        decode_PVFS_offset(pptr, &(rp+i)->lb);
        decode_PVFS_size(pptr, &(rp+i)->aggregate_size);
        decode_int32_t(pptr, &(rp+i)->num_contig_chunks);
        decode_int32_t(pptr, &(rp+i)->depth);
        decode_int32_t(pptr, &(rp+i)->num_nested_req);
        decode_int32_t(pptr, &(rp+i)->committed);
        decode_int32_t(pptr, &(rp+i)->refcount);
        decode_skip4(pptr,);

	/* put integer offsets into pointers, let PINT_Request_decode fix */
	decode_uint32_t(pptr, &encti);
	(rp+i)->ereq = (PINT_Request *)(uintptr_t) encti;

	decode_uint32_t(pptr, &encti);
	(rp+i)->sreq = (PINT_Request *)(uintptr_t) encti;
    }

    *req = rp;
    return 0;
}

#endif /* __PINT_REQPROTO_ENCODE_FUNCS_C */
#endif /* _PINT_REQUEST_ENCODE_H_ */
